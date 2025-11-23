/*
    each student record has:
        - student ID (integer)
        - name (string)
        - programme (string)
        - mark (float)

    main features:
        - OPEN:    read all records from a database file
        - SHOW ALL: display all current records
        - INSERT:  add a new student record (no duplicate IDs)
        - QUERY:   search for a record by ID
        - UPDATE:  change fields of an existing record
        - DELETE:  remove a record by ID (with confirmation)
        - SAVE:    write all records back to the database file

    enhancement features we added:
        - sorting:
            SHOW ALL SORT BY ID ASC / DESC
            SHOW ALL SORT BY MARK ASC / DESC
        - summary:
            SHOW SUMMARY  (total students, average, highest, lowest)
        - search:
            FIND NAME "..."       (case-insensitive substring search)
            FIND PROGRAMME "..."  (case-insensitive substring search)
        - CSV import/export:
            IMPORT CSV <file.csv>
            EXPORT CSV <file.csv>
        - SQL export:
            EXPORT SQL <file.sql>
        - backup:
            BACKUP  (creates timestamped backup of current database file)

    extra unique feature we added:
        - database password:
          When the program starts, after the declaration, the user must enter
          the correct password, or the CMS program will exit and nothing can be used.
*/

#include <stdio.h>      // printf, fgets, FILE, fopen, etc
#include <stdlib.h>     // malloc, realloc, free, strtol, strtof, exit
#include <string.h>     // strlen, strcpy, strncpy, strtok, strcmp, etc
#include <ctype.h>      // toupper, tolower, isspace
#include <time.h>       // time, localtime, strftime (for backup + declaration date)
#include <errno.h>      // errno, strerror (for error messages)

// for windows file path
#ifdef _WIN32
    #include <windows.h>  // GetModuleFileNameA
    #include <direct.h>   // _getcwd
    #define getcwd _getcwd
    #define PATH_SEP '\\'
#else
    // For linux / macOS file path
    #include <unistd.h>   // readlink, getcwd
    #include <limits.h>   // PATH_MAX
    #define PATH_SEP '/'
#endif

// our group name to show in the prompt and declaration
#define OUR_GROUP_NAME "P10-4"

// global constants to control lengths of strings
#define NAME_MAX_LENGTH 128
#define PROGRAMME_MAX_LENGTH 128
#define INITIAL_CAPACITY 128

// simple database password
#define DATABASE_PASSWORD "password"
#define MAX_PASSWORD_ATTEMPTS 3

// StudentRecord: represents one row in the StudentRecords table
typedef struct {
    int   id;                                     // student ID
    char  name[NAME_MAX_LENGTH];                  // student name
    char  programme[PROGRAMME_MAX_LENGTH];        // programme name
    float mark;                                   // mark
} StudentRecord;

// StudentTable:
// a simple dynamic array of StudentRecord
// - records: pointer to heap memory
// - count: how many records currently used
// - capacity: how many records allocated
typedef struct {
    StudentRecord *records;
    size_t count;
    size_t capacity;
} StudentTable;

// global student table used by the whole program
static StudentTable studentTable = { NULL, 0, 0 };

// remember last opened/saved database file name (the logical name typed by user)
static char lastDatabaseFileName[256] = { 0 };

// store the folder where the program’s .exe is located
// used so that exports and backups are placed next to the executable
static char programDirectoryPath[1024] = { 0 };

// removes whitespace at the start and end of a string
// whitespace includes space, tab, newline, etc
// an example: "  hello \n"  ->  "hello"
static void trimSpaces(char *s)
{
    // first, trim leading spaces
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) {
        i++;
    }
    if (i > 0) {
        // shift the string to the left to remove the leading spaces
        memmove(s, s + i, strlen(s + i) + 1);
    }

    // then, trim trailing spaces
    int j = (int)strlen(s) - 1;
    while (j >= 0 && isspace((unsigned char)s[j])) {
        s[j] = '\0';
        j--;
    }
}

// compare two strings after making it all lowercase so case dont matter
static int equalsIgnoreCase(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

// check if "needle" is inside "haystack", ignoring case.
// used for FIND NAME and FIND PROGRAMME.
// returns pointer to first match inside haystack, or NULL if not found.
static const char *containsIgnoreCase(const char *haystack, const char *needle)
{
    if (!*needle) {
        return haystack;    // empty needle always "found" at start
    }

    size_t needleLength = strlen(needle);

    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < needleLength && p[i] && 
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == needleLength) {
            return p;       // found full needle
        }
    }
    return NULL;
}

// from a full path, extract only the file name without extension.
// e.g.  "/a/b/db.txt" -> "db"
static void getFileNameStem(const char *path, char *out, size_t outSize)
{
    const char *slash1 = strrchr(path, '/');
    const char *slash2 = strrchr(path, '\\');
    const char *slash = slash1 ? (slash2 && slash2 > slash1 ? slash2 : slash1) : slash2;

    const char *baseName = slash ? slash + 1 : path;
    const char *dot = strrchr(baseName, '.');

    size_t length = dot ? (size_t)(dot - baseName) : strlen(baseName);
    if (length >= outSize) {
        length = outSize - 1;
    }

    memcpy(out, baseName, length);
    out[length] = '\0';
}

// check if a path is relative or absolute
// for windows: - treat "C:\..." or "\something" or "/something" as absolute
// for linux/macOS: - treat "/something" as absolute
static int isPathRelative(const char *p)
{
#ifdef _WIN32
    if (strlen(p) >= 2 && p[1] == ':') return 0;    // "C:\..."
    if (p[0] == '\\' || p[0] == '/') return 0;      // "\foo" or "/foo"
    return 1;
#else
    return p[0] != '/';
#endif
}

// this function is to build "dir/filename" or "dir\filename" safely
static void joinPath(char *out, size_t outSize, const char *dir, const char *file)
{
    size_t dirLength = strlen(dir);
    int needSeparator = (dirLength > 0 && dir[dirLength - 1] != PATH_SEP);

    if (needSeparator) {
        snprintf(out, outSize, "%s%c%s", dir, PATH_SEP, file);
    } else {
        snprintf(out, outSize, "%s%s", dir, file);
    }
}

// find the directory where the executable (.exe) is located
// windows: Use GetModuleFileNameA to get full path of program, then cut off file name
// on linux/macOS: use readlink("/proc/self/exe", ...) to get full path, then cut off file name
static void fillProgramDirectoryPath(char *out, size_t outSize)
{
#ifdef _WIN32
    char buffer[1024];
    DWORD length = GetModuleFileNameA(NULL, buffer, (DWORD)sizeof(buffer));
    if (length == 0 || length >= sizeof(buffer)) {
        out[0] = '\0';
        return;
    }

    char *lastSlash = strrchr(buffer, '\\');
    if (lastSlash) {
        *lastSlash = '\0';     // remove "\program.exe"
    }

    strncpy(out, buffer, outSize - 1);
    out[outSize - 1] = '\0';
#else
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        out[0] = '\0';
        return;
    }

    buffer[length] = '\0';
    char *lastSlash = strrchr(buffer, '/');
    if (lastSlash) {
        *lastSlash = '\0';     // remove "/program"
    }

    strncpy(out, buffer, outSize - 1);
    out[outSize - 1] = '\0';
#endif
}

// convert string to int safely using strtol
static int stringToInt(const char *s, int *out)
{
    char *end = NULL;
    long value = strtol(s, &end, 10);
    if (s == end) {
        return 0;   // no digits found
    }
    *out = (int)value;
    return 1;
}

// convert string to float safely using strtof
static int stringToFloat(const char *s, float *out)
{
    char *end = NULL;
    float value = strtof(s, &end);
    if (s == end) {
        return 0;   // no digits found
    }
    *out = value;
    return 1;
}

/*
from a command string like the following:
    "INSERT ID=2301234 Name=\"Brian\" Programme=\"Digital Supply Chain\" Mark=88.8"
    extract the value after a given key (e.g. "ID", "Name", "Programme", "Mark")
    - supports values with spaces if quoted using double quotes
    - case-insensitive for the key
returns:
    1 if key found and value extracted, 0 if not found or invalid
*/
static int readKeyValueFromCommand(const char *src,
                                   const char *key,
                                   char *out,
                                   size_t outLen)
{
    const char *p = src;
    size_t keyLength = strlen(key);

    while ((p = containsIgnoreCase(p, key))) {
        // makes sure character before key is space (or start of string)
        if (p != src && !isspace((unsigned char)p[-1])) {
            p++;
            continue;
        }

        // move to the position after key, skip spaces
        const char *eq = p + keyLength;
        while (*eq && isspace((unsigned char)*eq)) {
            eq++;
        }

        if (*eq != '=') {
            p++;
            continue;   // not really a "key=" pattern
        }

        eq++;   // skip '='

        // skip spaces after '='
        while (*eq && isspace((unsigned char)*eq)) {
            eq++;
        }

        // if first character is a quote, read until closing quote
        if (*eq == '"') {
            eq++;   // skip opening quote
            const char *end = strchr(eq, '"');
            if (!end) {
                return 0;   // no closing quote
            }

            size_t len = (size_t)(end - eq);
            if (len >= outLen) {
                len = outLen - 1;
            }

            memcpy(out, eq, len);
            out[len] = '\0';
            return 1;
        } else {
            // unquoted value: read until next whitespace
            size_t i = 0;
            while (*eq && !isspace((unsigned char)*eq) && i + 1 < outLen) {
                out[i++] = *eq++;
            }
            out[i] = '\0';
            return i > 0;
        }
    }

    return 0;
}

// find student by ID in the global studentTable
static int findIndexById(int id)
{
    for (size_t i = 0; i < studentTable.count; ++i) {
        if (studentTable.records[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

// allocate initial memory for the student table.
// - set capacity to INITIAL_CAPACITY
// - set count to 0
// - allocate memory for that many StudentRecord
static void studentTableInit(StudentTable *table)
{
    table->capacity = INITIAL_CAPACITY;
    table->count = 0;
    table->records = (StudentRecord *)malloc(table->capacity * sizeof(StudentRecord));
    if (!table->records) {
        fprintf(stderr, "CMS: Out of memory when creating student table.\n");
        exit(1);
    }
}

// free all memory used by the student table. (calls this at program end)
static void studentTableFree(StudentTable *table)
{
    free(table->records);
    table->records = NULL;
    table->count = 0;
    table->capacity = 0;
}

// make sure there is space for at least one more student record
// if full, double the capacity using realloc
static void ensureStudentTableCapacity(StudentTable *table)
{
    if (table->count >= table->capacity) {
        table->capacity *= 2;
        StudentRecord *newMemory =
            (StudentRecord *)realloc(table->records, table->capacity * sizeof(StudentRecord));
        if (!newMemory) {
            fprintf(stderr, "CMS: Out of memory when expanding student table.\n");
            exit(1);
        }
        table->records = newMemory;
    }
}

// insert a new student record into the table, if ID is not duplicated
static int addStudentRecord(int id,
                            const char *name,
                            const char *programme,
                            float mark)
{
    // Check duplicate ID
    if (findIndexById(id) != -1) {
        return 0; // a record with the same ID already exists
    }

    // fill in a StudentRecord struct
    StudentRecord newStudent;
    newStudent.id = id;
    newStudent.mark = mark;

    strncpy(newStudent.name, name ? name : "", NAME_MAX_LENGTH - 1);
    newStudent.name[NAME_MAX_LENGTH - 1] = '\0';

    strncpy(newStudent.programme, programme ? programme : "", PROGRAMME_MAX_LENGTH - 1);
    newStudent.programme[PROGRAMME_MAX_LENGTH - 1] = '\0';

    // ensure we have capacity, then append record
    ensureStudentTableCapacity(&studentTable);
    studentTable.records[studentTable.count++] = newStudent;

    return 1; // record inserted successfully
}

// get a pointer to the StudentRecord with the given ID
// returns the pointer to StudentRecord if found, or NULL if not found
static StudentRecord *getStudentRecordById(int id)
{
    int index = findIndexById(id);
    return (index == -1) ? NULL : &studentTable.records[index];
}

/*
update only the fields that the user provided
parameters:
    id         : student ID to update
    newName    : new name or NULL (if not updating name)
    newProgramme: new programme or NULL
    newMarkPtr : pointer to new mark, or NULL if not updating mark

returns:
        1 if record updated successfully
        0 if record with given ID does not exist
*/
static int updateStudentRecord(int id,
                               const char *newName,
                               const char *newProgramme,
                               const float *newMarkPtr)
{
    int index = findIndexById(id);
    if (index == -1) {
        return 0;
    }

    if (newName) {
        strncpy(studentTable.records[index].name, newName, NAME_MAX_LENGTH - 1);
        studentTable.records[index].name[NAME_MAX_LENGTH - 1] = '\0';
    }

    if (newProgramme) {
        strncpy(studentTable.records[index].programme, newProgramme, PROGRAMME_MAX_LENGTH - 1);
        studentTable.records[index].programme[PROGRAMME_MAX_LENGTH - 1] = '\0';
    }

    if (newMarkPtr) {
        studentTable.records[index].mark = *newMarkPtr;
    }

    return 1;
}

/*
remove a student record with the given ID
internally, we shift all later records left by one
*/
static int deleteStudentRecord(int id)
{
    int index = findIndexById(id);
    if (index == -1) {
        return 0; //if record not found
    }

    // Shift every record after "index" one step to the left
    for (size_t i = (size_t)index + 1; i < studentTable.count; ++i) {
        studentTable.records[i - 1] = studentTable.records[i];
    }

    studentTable.count--;
    return 1; //if deleted successfully,
}


/*
try to open a file for reading

1. try the name exactly as the user typed.
2. if fail and the name is relative, also try "<programDirectoryPath>/<name>".

parameters:
    fileName   : what user typed
    actualUsed : will point to the path that successfully opened (for debug, if needed)
*/
static FILE *openFileForReadSearch(const char *fileName, const char **actualUsed)
{
    FILE *fp = fopen(fileName, "r");
    *actualUsed = fileName;

    if (!fp && isPathRelative(fileName) && programDirectoryPath[0]) {
        static char alternativePath[1024];
        joinPath(alternativePath, sizeof(alternativePath), programDirectoryPath, fileName);

        fp = fopen(alternativePath, "r");
        if (fp) {
            *actualUsed = alternativePath;
        }
    }

    return fp; //if opened successfully
}

/*
open a file for writing. if the file name is relative, we write it into
the same folder as the .exe / if absolute, use as it is.

parameters:
    fileName      : logical file name (e.g. "P10-4-CMS.txt" or "output.csv")
    actualPathOut : buffer to store actual full path
    pathCapacity  : size of buffer
*/
static FILE *openFileForWriteInProgramFolder(const char *fileName,
                                             char *actualPathOut,
                                             size_t pathCapacity)
{
    if (!fileName || !fileName[0]) {
        return NULL;
    }

    if (isPathRelative(fileName) && programDirectoryPath[0]) {
        joinPath(actualPathOut, pathCapacity, programDirectoryPath, fileName);
        return fopen(actualPathOut, "w");
    } else {
        if (pathCapacity) {
            strncpy(actualPathOut, fileName, pathCapacity - 1);
            actualPathOut[pathCapacity - 1] = '\0';
        }
        return fopen(fileName, "w");
    }
}

/*
read a tab-separated database file into the global studentTable

file format (TSV):
    ID<TAB>Name<TAB>Programme<TAB>Mark
*/
static int loadDatabaseFromFile(const char *fileName)
{
    const char *usedPath = NULL;

    FILE *fp = openFileForReadSearch(fileName, &usedPath);
    if (!fp) {
        char cwd[1024];

        if (!getcwd(cwd, sizeof(cwd))) {
            strncpy(cwd, "<unknown>", sizeof(cwd));
        }

        fprintf(stderr, "CMS: fopen failed for \"%s\": %s\n",
                fileName, strerror(errno));
        fprintf(stderr, "CMS: Current working directory: %s\n", cwd);
        fprintf(stderr, "CMS: Executable directory   : %s\n",
                programDirectoryPath[0] ? programDirectoryPath : "<unknown>");

        return 0;
    }

    // reset table before loading new data
    studentTable.count = 0;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        trimSpaces(line);
        if (!line[0]) {
            continue;   // skip empty lines
        }

        // split by TAB into 4 tokens
        char *tokenId   = strtok(line, "\t");
        char *tokenName = strtok(NULL, "\t");
        char *tokenProg = strtok(NULL, "\t");
        char *tokenMark = strtok(NULL, "\t");

        if (!tokenId || !tokenName || !tokenProg || !tokenMark) {
            continue;   // skip malformed lines
        }

        int   id   = atoi(tokenId);
        float mark = (float)atof(tokenMark);

        addStudentRecord(id, tokenName, tokenProg, mark);
    }

    fclose(fp);

    // remember file name logically (for SAVE with no argument)
    strncpy(lastDatabaseFileName, fileName, sizeof(lastDatabaseFileName) - 1);
    lastDatabaseFileName[sizeof(lastDatabaseFileName) - 1] = '\0';

    return 1;
}

// save studentTable into a tab-separated file
// if fileName is NULL or empty, we use lastDatabaseFileName
static int saveDatabaseToFile(const char *fileName)
{
    const char *logicalName =
        (fileName && fileName[0]) ? fileName : lastDatabaseFileName;

    if (!logicalName || !logicalName[0]) {
        return 0;   // we don't know where to save yet
    }

    char actualPath[1024];
    FILE *fp = openFileForWriteInProgramFolder(logicalName, actualPath, sizeof(actualPath));
    if (!fp) {
        return 0;
    }

    // each line: ID<TAB>Name<TAB>Programme<TAB>Mark
    for (size_t i = 0; i < studentTable.count; ++i) {
        StudentRecord *student = &studentTable.records[i];
        fprintf(fp, "%d\t%s\t%s\t%.1f\n",
                student->id,
                student->name,
                student->programme,
                student->mark);
    }

    fclose(fp);
    return 1;
}

// SUMMARY AND SORTING
typedef enum {
    SORT_NONE,
    SORT_BY_ID,
    SORT_BY_MARK
} SortField;

typedef enum {
    SORT_ASCENDING,
    SORT_DESCENDING
} SortDirection;

// comparator for qsort: compares students by ID ascending
static int compareByIdAscending(const void *a, const void *b)
{
    const StudentRecord *x = (const StudentRecord *)a;
    const StudentRecord *y = (const StudentRecord *)b;
    if (x->id > y->id) return 1;
    if (x->id < y->id) return -1;
    return 0;
}

// comparator for qsort: compares students by Mark ascending
static int compareByMarkAscending(const void *a, const void *b)
{
    const StudentRecord *x = (const StudentRecord *)a;
    const StudentRecord *y = (const StudentRecord *)b;
    if (x->mark > y->mark) return 1;
    if (x->mark < y->mark) return -1;
    return 0;
}

/*
print all records to the console
- optionally sort by ID or Mark
- optionally sort ascending or descending

implementation:
- we make a temporary copy of the student array
- sort the copy so the original order in memory is not changed
*/
static void showAllStudents(SortField field, SortDirection direction)
{
    // allocate temporary array
    StudentRecord *copy =
        (StudentRecord *)malloc(studentTable.count * sizeof(StudentRecord));
    if (!copy) {
        fprintf(stderr, "CMS: Out of memory in showAllStudents.\n");
        return;
    }

    // copy all records into temporary array
    for (size_t i = 0; i < studentTable.count; ++i) {
        copy[i] = studentTable.records[i];
    }

    // sort the temporary copy if requested
    if (field == SORT_BY_ID) {
        qsort(copy, studentTable.count, sizeof(StudentRecord), compareByIdAscending);
    } else if (field == SORT_BY_MARK) {
        qsort(copy, studentTable.count, sizeof(StudentRecord), compareByMarkAscending);
    }

    // if descending, reverse the array in place
    if (direction == SORT_DESCENDING) {
        for (size_t i = 0; i < studentTable.count / 2; ++i) {
            StudentRecord temp = copy[i];
            copy[i] = copy[studentTable.count - 1 - i];
            copy[studentTable.count - 1 - i] = temp;
        }
    }

    // print heading and all rows
    printf("CMS: Here are all the records found in the table \"StudentRecords\".\n");
    printf("ID Name Programme Mark\n");

    for (size_t i = 0; i < studentTable.count; ++i) {
        printf("%d %s %s %.1f\n",
               copy[i].id,
               copy[i].name,
               copy[i].programme,
               copy[i].mark);
    }

    free(copy);
}

/*
print summary statistics:
- total number of students
- average mark
- highest mark (with student name)
- lowest mark (with student name)
*/
static void showSummaryStatistics(void)
{
    if (studentTable.count == 0) {
        printf("CMS: No records loaded.\n");
        return;
    }

    size_t totalStudents = studentTable.count;
    float totalMark = 0.0f;

    float lowestMark = studentTable.records[0].mark;
    float highestMark = studentTable.records[0].mark;
    size_t indexLowest = 0;
    size_t indexHighest = 0;

    for (size_t i = 0; i < totalStudents; ++i) {
        float mark = studentTable.records[i].mark;
        totalMark += mark;

        if (mark < lowestMark) {
            lowestMark = mark;
            indexLowest = i;
        }
        if (mark > highestMark) {
            highestMark = mark;
            indexHighest = i;
        }
    }

    printf("CMS: SUMMARY\n");
    printf("Total students: %zu\n", totalStudents);
    printf("Average mark: %.2f\n", totalMark / (float)totalStudents);
    printf("Highest: %.1f (%s)\n", highestMark, studentTable.records[indexHighest].name);
    printf("Lowest : %.1f (%s)\n", lowestMark, studentTable.records[indexLowest].name);
}

// CSV / SQL IMPORT / EXPORT
/*
parse a line of CSV into 4 fields:
f0: ID
f1: Name
f2: Programme
f3: Mark
supports:
- quoted fields ("...") that may contain commas
- escaped quotes inside fields ("" -> ")
*/
static int csvSplitLineInto4Fields(const char *line,
                                   char *f0, size_t n0,
                                   char *f1, size_t n1,
                                   char *f2, size_t n2,
                                   char *f3, size_t n3)
{
    const char *p = line;

    char *outputs[4] = { f0, f1, f2, f3 };
    size_t caps[4]   = { n0, n1, n2, n3 };

    for (int col = 0; col < 4; ++col) {
        // skip spaces before field
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        // check if field is quoted
        if (*p == '"') {
            p++;  // skip opening quote
            size_t k = 0;

            while (*p) {
                if (*p == '"') {
                    if (*(p + 1) == '"') {
                        // double quote inside field -> single quote in value
                        if (k + 1 < caps[col]) {
                            outputs[col][k++] = '"';
                        }
                        p += 2;
                    } else {
                        // end of quoted field
                        p++;
                        // skip spaces after quote
                        while (*p == ' ' || *p == '\t') {
                            p++;
                        }
                        if (col < 3) {
                            if (*p != ',') return 0;
                            p++;    // skip comma
                        }
                        break;
                    }
                } else {
                    if (k + 1 < caps[col]) {
                        outputs[col][k++] = *p;
                    }
                    p++;
                }
            }

            outputs[col][k] = '\0';
        } else {
            // unquoted field: read until comma or end-of-line
            size_t k = 0;
            while (*p && *p != ',' && *p != '\r' && *p != '\n') {
                if (k + 1 < caps[col]) {
                    outputs[col][k++] = *p;
                }
                p++;
            }
            outputs[col][k] = '\0';

            if (col < 3) {
                if (*p != ',') return 0;
                p++;    // skip comma
            }
        }
    }

    // ignore trailing whitespace/newlines
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }

    // if extra text after 4 fields, treat as error
    if (*p != '\0') {
        return 0;
    }

    return 1;
}

/*
export current studentTable to a CSV file in the program folder
format of exporting:
    header:
        ID,Name,Programme,Mark
    rows:
        id,"name","programme",mark
*/
static int exportToCsvFile(const char *csvFileName)
{
    if (!csvFileName || !csvFileName[0]) {
        return 0;
    }

    char actualPath[1024];
    FILE *fp = openFileForWriteInProgramFolder(csvFileName, actualPath, sizeof(actualPath));
    if (!fp) {
        return 0;
    }

    // header
    fprintf(fp, "ID,Name,Programme,Mark\n");

    for (size_t i = 0; i < studentTable.count; ++i) {
        const StudentRecord *student = &studentTable.records[i];

        fprintf(fp, "%d,\"", student->id);

        // write name, escaping quotes
        for (const char *c = student->name; *c; ++c) {
            if (*c == '"') fputc('"', fp);
            fputc(*c, fp);
        }

        fprintf(fp, "\",\"");

        // write programme, escaping quotes
        for (const char *c = student->programme; *c; ++c) {
            if (*c == '"') fputc('"', fp);
            fputc(*c, fp);
        }

        fprintf(fp, "\",%.1f\n", student->mark);
    }

    fclose(fp);
    return 1;
}

/*
export studentTable as SQL statements:
- DROP TABLE IF EXISTS StudentRecords;
- CREATE TABLE ...
- INSERT INTO StudentRecords(...) VALUES(...);

file is saved next to the .exe (programDirectoryPath)
*/
static int exportToSqlFile(const char *sqlFileName)
{
    if (!sqlFileName || !sqlFileName[0]) {
        return 0;
    }

    char actualPath[1024];
    FILE *fp = openFileForWriteInProgramFolder(sqlFileName, actualPath, sizeof(actualPath));
    if (!fp) {
        return 0;
    }

    fprintf(fp, "-- SQL dump generated by CMS\n");
    fprintf(fp, "DROP TABLE IF EXISTS StudentRecords;\n");
    fprintf(fp, "CREATE TABLE StudentRecords (\n"
                "  id INTEGER PRIMARY KEY,\n"
                "  name TEXT NOT NULL,\n"
                "  programme TEXT NOT NULL,\n"
                "  mark REAL NOT NULL\n"
                ");\n");

    for (size_t i = 0; i < studentTable.count; ++i) {
        const StudentRecord *student = &studentTable.records[i];

        // escape single quotes for SQL
        char escapedName[2 * NAME_MAX_LENGTH] = { 0 };
        char escapedProgramme[2 * PROGRAMME_MAX_LENGTH] = { 0 };

        size_t k = 0;
        for (size_t j = 0; j < strlen(student->name) && k + 2 < sizeof(escapedName); ++j) {
            if (student->name[j] == '\'') {
                escapedName[k++] = '\'';
                escapedName[k++] = '\'';
            } else {
                escapedName[k++] = student->name[j];
            }
        }

        k = 0;
        for (size_t j = 0; j < strlen(student->programme) && k + 2 < sizeof(escapedProgramme); ++j) {
            if (student->programme[j] == '\'') {
                escapedProgramme[k++] = '\'';
                escapedProgramme[k++] = '\'';
            } else {
                escapedProgramme[k++] = student->programme[j];
            }
        }

        fprintf(fp,
                "INSERT INTO StudentRecords(id,name,programme,mark) "
                "VALUES(%d,'%s','%s',%.1f);\n",
                student->id,
                escapedName,
                escapedProgramme,
                student->mark);
    }

    fclose(fp);
    return 1;
}

/*
read students from a CSV file and add them into studentTable
there is a check for the following:
- first line may be a header "ID,Name,Programme,Mark" (case-insensitive)
- lines may be quoted
- if a student ID already exists, that record is skipped (no duplicate)
*/
static int importFromCsvFile(const char *csvFileName)
{
    const char *usedPath = NULL;
    FILE *fp = openFileForReadSearch(csvFileName, &usedPath);
    if (!fp) {
        return 0;
    }

    char line[2048];

    // read first line (might be header or first data row)
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 0;
    }

    {
        char temp[2048];
        strncpy(temp, line, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        trimSpaces(temp);

        if (temp[0] != '\0') {
            char f0[64], f1[NAME_MAX_LENGTH], f2[PROGRAMME_MAX_LENGTH], f3[64];

            if (csvSplitLineInto4Fields(temp, f0, sizeof(f0),
                                        f1, sizeof(f1),
                                        f2, sizeof(f2),
                                        f3, sizeof(f3))) {
                // check if this line is the header "ID,Name,Programme,Mark"
                if (equalsIgnoreCase(f0, "ID") &&
                    equalsIgnoreCase(f1, "Name") &&
                    equalsIgnoreCase(f2, "Programme") &&
                    equalsIgnoreCase(f3, "Mark")) {
                    // it is a header, so we ignore it and move on
                } else {
                    // not a header, so treat this as actual data
                    int   id   = atoi(f0);
                    float mark = (float)atof(f3);

                    if (findIndexById(id) == -1) {
                        addStudentRecord(id, f1, f2, mark);
                    }
                }
            }
        }
    }

    // read remaining lines
    while (fgets(line, sizeof(line), fp) != NULL) {
        char trimmed[2048];
        strncpy(trimmed, line, sizeof(trimmed) - 1);
        trimmed[sizeof(trimmed) - 1] = '\0';
        trimSpaces(trimmed);

        if (trimmed[0] == '\0') {
            continue;   // skip empty line
        }

        char f0[64], f1[NAME_MAX_LENGTH], f2[PROGRAMME_MAX_LENGTH], f3[64];
        if (!csvSplitLineInto4Fields(trimmed, f0, sizeof(f0),
                                     f1, sizeof(f1),
                                     f2, sizeof(f2),
                                     f3, sizeof(f3))) {
            continue;   // skip malformed line
        }

        // extra safety precaution: skip repeated header mid-file
        if (equalsIgnoreCase(f0, "ID") &&
            equalsIgnoreCase(f1, "Name") &&
            equalsIgnoreCase(f2, "Programme") &&
            equalsIgnoreCase(f3, "Mark")) {
            continue;
        }

        int   id   = atoi(f0);
        float mark = (float)atof(f3);

        if (findIndexById(id) == -1) {
            addStudentRecord(id, f1, f2, mark);
        }
    }

    fclose(fp);
    return 1;
}

/*

simple case-insensitive search by Name or Programme
the parameters:
    fieldName: "NAME" or "PROGRAMME"
    needle   : search text (substring)
example:
    FIND NAME "brian"
    FIND PROGRAMME "Digital Supply Chain"
*/
static void findStudentsByField(const char *fieldName, const char *needle)
{
    if (!needle || !needle[0]) {
        printf("CMS: Please provide a search string.\n");
        return;
    }

    printf("CMS: Search results for %s contains \"%s\":\n", fieldName, needle);
    printf("ID Name Programme Mark\n");

    int hitCount = 0;

    for (size_t i = 0; i < studentTable.count; ++i) {
        if (equalsIgnoreCase(fieldName, "NAME") &&
            containsIgnoreCase(studentTable.records[i].name, needle)) {
            printf("%d %s %s %.1f\n",
                   studentTable.records[i].id,
                   studentTable.records[i].name,
                   studentTable.records[i].programme,
                   studentTable.records[i].mark);
            hitCount++;
        } else if (equalsIgnoreCase(fieldName, "PROGRAMME") &&
                   containsIgnoreCase(studentTable.records[i].programme, needle)) {
            printf("%d %s %s %.1f\n",
                   studentTable.records[i].id,
                   studentTable.records[i].name,
                   studentTable.records[i].programme,
                   studentTable.records[i].mark);
            hitCount++;
        }
    }

    if (!hitCount) {
        printf("(no matches)\n");
    }
}

/*
function to create a timestamped backup of the current database file
eg:
    if lastDatabaseFileName is "P10-4-CMS.txt", backup file might be "P10-4-CMS.bak-20251125-153012.txt".
the implementation:
- use getFileNameStem to get base name without extension
- append ".bak-YYYYMMDD-HHMMSS.txt"
- reuse saveDatabaseToFile() to create the backup content
*/
static int makeTimestampedBackup(void)
{
    if (!lastDatabaseFileName[0]) {
        return 0;   // we need to know which file is the "current" DB
    }

    char stem[256];
    getFileNameStem(lastDatabaseFileName, stem, sizeof(stem));

    time_t now = time(NULL);
    struct tm *tmPtr = localtime(&now);

    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tmPtr);

    char backupFileName[512];
    snprintf(backupFileName, sizeof(backupFileName),
             "%s.bak-%s.txt", stem, timestamp);

    return saveDatabaseToFile(backupFileName);
}


// print our declaration at the start of the program
static void printDeclaration(void)
{
    //time_t now = time(NULL);
    //struct tm *tmPtr = localtime(&now);
    const char *dateString = "24/11/2025"; //change to hard code for submission date

    //char dateString[32];
    //strftime(dateString, sizeof(dateString), "%Y-%m-%d", tmPtr);
    printf("Date of submission: %s\n\n", dateString);

    printf("\nDeclaration\n");
    printf("SIT's policy on copying does not allow the students to copy source code as well as assessment solutions\n");
    printf("from another person AI or other places. It is the students' responsibility to guarantee that their\n");
    printf("assessment solutions are their own work. Meanwhile, the students must also ensure that their work is\n");
    printf("not accessible by others. Where such plagiarism is detected, both of the assessments involved will\n");
    printf("receive ZERO mark.\n\n");

    printf("We hereby declare that:\n");
    printf("- We fully understand and agree to the abovementioned plagiarism policy.\n");
    printf("- We did not copy any code from others or from other places.\n");
    printf("- We did not share our codes with others or upload to any other places for public access and will not do that in the future.\n");
    printf("- We agree that our project will receive Zero mark if there is any plagiarism detected.\n");
    printf("- We agree that we will not disclose any information or material of the group project to others or upload to any other places for public access.\n");
    printf("- We agree that we did not copy any code directly from AI generated sources.\n\n");

    printf("Declared by: %s\n", OUR_GROUP_NAME);
    printf("Team members:\n");
    printf("1. BRIAN GOH JUN WEI\n");
    printf("2. HAN YONG\n");
    printf("3. JERREL\n");
    printf("4. KENDRICK\n");
    printf("5. XIAN YANG\n");
    printf("Date: %s\n\n", dateString);
}

/*
prompts the user to enter the database password before using our CMS
if correct password is entered within MAX_PASSWORD_ATTEMPTS,
return 1 (success).
if all attempts fail, return 0 and the program will exit

additional unique feature (just a simple standard authentication for databases i guess?)
*/
static int checkDatabasePassword(void)
{
    char inputBuffer[256];

    for (int attempt = 1; attempt <= MAX_PASSWORD_ATTEMPTS; ++attempt) {
        printf("Please enter database password to continue (attempt %d of %d): ",
               attempt, MAX_PASSWORD_ATTEMPTS);

        if (!fgets(inputBuffer, sizeof(inputBuffer), stdin)) {
            printf("\nCMS: Input error.\n");
            return 0;
        }

        trimSpaces(inputBuffer);

        if (strcmp(inputBuffer, DATABASE_PASSWORD) == 0) {
            printf("CMS: Password accepted. Welcome to the Class Management System.\n\n");
            return 1;
        } else {
            printf("CMS: Incorrect password.\n");
        }
    }

    printf("CMS: Too many invalid password attempts. Exiting program.\n");
    return 0;
}

// show all commands supported by this program, with examples (to allow user to just copy paste)
static void printHelp(void)
{
    puts("Commands (examples included!):\n");

    puts("OPEN / SAVE");
    puts("  OPEN <file>                 e.g.  OPEN db.txt");
    puts("  SAVE                        (saves back to last OPEN file)");
    puts("  SAVE <file>                 e.g.  SAVE db.txt\n");

    puts("VIEW");
    puts("  SHOW ALL                    list all rows");
    puts("  SHOW ALL SORT BY ID ASC     or DESC");
    puts("  SHOW ALL SORT BY MARK ASC   or DESC");
    puts("  SHOW SUMMARY                show count/average/highest/lowest\n");

    puts("ADD / LOOKUP / EDIT / REMOVE");
    puts("  INSERT ID=<int> Name=\"...\" Programme=\"...\" Mark=<float>");
    puts("    e.g. INSERT ID=2501066 Name=\"Brian Goh\" Programme=\"Digital Supply Chain\" Mark=88.8");
    puts("  QUERY ID=<int>              e.g. QUERY ID=2501066");
    puts("  UPDATE ID=<int> [Name=...] [Programme=...] [Mark=<float>]");
    puts("    e.g. UPDATE ID=2501066 Programme=\"Game Development\" Mark=95.5");
    puts("  DELETE ID=<int>             comes with Y/N confirmation\n");

    puts("SEARCH");
    puts("  FIND NAME \"...\"         e.g. FIND NAME \"brian\"");
    puts("  FIND PROGRAMME \"...\"    e.g. FIND PROGRAMME \"Digital Supply Chain\"\n");

    puts("IMPORT / EXPORT / BACKUP");
    puts("  IMPORT CSV <file.csv>       Header in CSV must be: ID,Name,Programme,Mark");
    puts("  EXPORT CSV <file.csv>       Open in Excel/Sheets to verify");
    puts("  EXPORT SQL <file.sql>       SQLite/MySQL compatible INSERTs");
    puts("  BACKUP                      writes <stem>.bak-YYYYMMDD-HHMMSS.txt\n");

    puts("OTHER");
    puts("  HELP");
    puts("  EXIT\n");
}

/*
main interactive command loop
steps per iteration:
    1. print prompt: "<OUR_GROUP_NAME>: "
    2. read a full line from user
    3. convert a copy to uppercase for command matching
    4. compare and execute the correct command
    5. Loop until user types EXIT or QUIT
*/
static void runCommandShell(void)
{
    char line[1024];

    while (1) {
        // display prompt (which will be our group number)
        printf(OUR_GROUP_NAME ": ");

        // read one line from stdin; break on EOF (Ctrl+D / Ctrl+Z)
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        // remove extra spaces and skip empty lines
        trimSpaces(line);
        if (!line[0]) {
            continue;
        }

        // uppercase copy for easy comparison of commands
        char upperLine[1024];
        strncpy(upperLine, line, sizeof(upperLine) - 1);
        upperLine[sizeof(upperLine) - 1] = '\0';

        for (size_t i = 0; i < strlen(upperLine); ++i) {
            upperLine[i] = (char)toupper((unsigned char)upperLine[i]);
        }

        // EXIT / QUIT
        if (equalsIgnoreCase(upperLine, "EXIT") ||
            equalsIgnoreCase(upperLine, "QUIT")) {
            break;
        }

        // HELP
        else if (strncmp(upperLine, "HELP", 4) == 0) {
            printHelp();
        }

        // OPEN <file>
        else if (strncmp(upperLine, "OPEN", 4) == 0) {
            char *p = line + 4;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }

            if (!*p) {
                printf("CMS: Please provide a filename.\n");
                continue;
            }

            char *fileName = p;
            if (*p == '"') {
                p++;
                char *endQuote = strrchr(p, '"');
                if (endQuote) {
                    *endQuote = '\0';
                }
                fileName = p;
            }

            if (loadDatabaseFromFile(fileName)) {
                printf("CMS: The database file \"%s\" is successfully opened.\n", fileName);
            } else {
                printf("CMS: Failed to open file \"%s\".\n", fileName);
            }
        }

        // SAVE [file]
        else if (strncmp(upperLine, "SAVE", 4) == 0) {
            char *p = line + 4;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }

            const char *fileName = *p ? p : NULL;  // NULL means reuse lastDatabaseFileName

            if (saveDatabaseToFile(fileName)) {
                printf("CMS: The database file is successfully saved.\n");
            } else {
                printf("CMS: Failed to save. Please OPEN a file first or provide a filename.\n");
            }
        }

        // SHOW ALL [SORT BY ID|MARK ASC|DESC]
        else if (strncmp(upperLine, "SHOW ALL", 8) == 0) {
            SortField field = SORT_NONE;
            SortDirection direction = SORT_ASCENDING;

            if (strstr(upperLine, "SORT BY ID")) {
                field = SORT_BY_ID;
                direction = strstr(upperLine, "DESC") ? SORT_DESCENDING : SORT_ASCENDING;
            } else if (strstr(upperLine, "SORT BY MARK")) {
                field = SORT_BY_MARK;
                direction = strstr(upperLine, "DESC") ? SORT_DESCENDING : SORT_ASCENDING;
            }

            showAllStudents(field, direction);
        }

        // INSERT ID=... Name="..." Programme="..." Mark=...
        else if (strncmp(upperLine, "INSERT", 6) == 0) {
            char idBuffer[64] = "";
            char nameBuffer[NAME_MAX_LENGTH] = "";
            char programmeBuffer[PROGRAMME_MAX_LENGTH] = "";
            char markBuffer[64] = "";

            if (!readKeyValueFromCommand(line, "ID", idBuffer, sizeof(idBuffer))) {
                printf("CMS: Missing ID=\n");
                continue;
            }
            if (!readKeyValueFromCommand(line, "Name", nameBuffer, sizeof(nameBuffer))) {
                printf("CMS: Missing Name=\n");
                continue;
            }
            if (!readKeyValueFromCommand(line, "Programme", programmeBuffer, sizeof(programmeBuffer))) {
                printf("CMS: Missing Programme=\n");
                continue;
            }
            if (!readKeyValueFromCommand(line, "Mark", markBuffer, sizeof(markBuffer))) {
                printf("CMS: Missing Mark=\n");
                continue;
            }

            int id;
            float mark;
            if (!stringToInt(idBuffer, &id)) {
                printf("CMS: Invalid ID.\n");
                continue;
            }
            if (!stringToFloat(markBuffer, &mark)) {
                printf("CMS: Invalid Mark.\n");
                continue;
            }

            if (addStudentRecord(id, nameBuffer, programmeBuffer, mark)) {
                printf("CMS: A new record with ID=%d is successfully inserted.\n", id);
            } else {
                printf("CMS: The record with ID=%d already exists.\n", id);
            }
        }

        // QUERY ID=...
        else if (strncmp(upperLine, "QUERY", 5) == 0) {
            char idBuffer[64] = "";

            if (!readKeyValueFromCommand(line, "ID", idBuffer, sizeof(idBuffer))) {
                printf("CMS: Missing ID=\n");
                continue;
            }

            int id;
            if (!stringToInt(idBuffer, &id)) {
                printf("CMS: Invalid ID.\n");
                continue;
            }

            StudentRecord *student = getStudentRecordById(id);
            if (!student) {
                printf("CMS: The record with ID=%d does not exist.\n", id);
            } else {
                printf("CMS: The record with ID=%d is found in the data table.\n", id);
                printf("ID Name Programme Mark\n");
                printf("%d %s %s %.1f\n",
                       student->id,
                       student->name,
                       student->programme,
                       student->mark);
            }
        }

        // UPDATE ID=... [Name=...] [Programme=...] [Mark=...]
        else if (strncmp(upperLine, "UPDATE", 6) == 0) {
            char idBuffer[64] = "";
            char nameBuffer[NAME_MAX_LENGTH] = "";
            char programmeBuffer[PROGRAMME_MAX_LENGTH] = "";
            char markBuffer[64] = "";

            if (!readKeyValueFromCommand(line, "ID", idBuffer, sizeof(idBuffer))) {
                printf("CMS: Missing ID=\n");
                continue;
            }

            int id;
            if (!stringToInt(idBuffer, &id)) {
                printf("CMS: Invalid ID.\n");
                continue;
            }

            int hasName = readKeyValueFromCommand(line, "Name", nameBuffer, sizeof(nameBuffer));
            int hasProgramme = readKeyValueFromCommand(line, "Programme", programmeBuffer, sizeof(programmeBuffer));
            int hasMark = readKeyValueFromCommand(line, "Mark", markBuffer, sizeof(markBuffer));

            float markValue;
            float *markPtr = NULL;

            if (hasMark) {
                if (!stringToFloat(markBuffer, &markValue)) {
                    printf("CMS: Invalid Mark.\n");
                    continue;
                }
                markPtr = &markValue;
            }

            if (updateStudentRecord(id,
                                    hasName ? nameBuffer : NULL,
                                    hasProgramme ? programmeBuffer : NULL,
                                    markPtr)) {
                printf("CMS: The record with ID=%d is successfully updated.\n", id);
            } else {
                printf("CMS: The record with ID=%d does not exist.\n", id);
            }
        }

        // DELETE ID=...
        else if (strncmp(upperLine, "DELETE", 6) == 0) {
            char idBuffer[64] = "";

            if (!readKeyValueFromCommand(line, "ID", idBuffer, sizeof(idBuffer))) {
                printf("CMS: Missing ID=\n");
                continue;
            }

            int id;
            if (!stringToInt(idBuffer, &id)) {
                printf("CMS: Invalid ID.\n");
                continue;
            }

            if (findIndexById(id) == -1) {
                printf("CMS: The record with ID=%d does not exist.\n", id);
                continue;
            }

            printf("CMS: Type Y to Confirm or N to cancel: ");

            char yesNoBuffer[16];
            if (!fgets(yesNoBuffer, sizeof(yesNoBuffer), stdin)) {
                printf("\n");
                continue;
            }

            trimSpaces(yesNoBuffer);

            if (yesNoBuffer[0] == 'Y' || yesNoBuffer[0] == 'y') {
                if (deleteStudentRecord(id)) {
                    printf("CMS: The record with ID=%d is successfully deleted.\n", id);
                } else {
                    printf("CMS: Delete failed.\n");
                }
            } else {
                printf("CMS: Delete cancelled.\n");
            }
        }

        // SHOW SUMMARY
        else if (strncmp(upperLine, "SHOW SUMMARY", 12) == 0) {
            showSummaryStatistics();
        }

        // EXPORT CSV <file.csv>
        else if (strncmp(upperLine, "EXPORT CSV", 10) == 0) {
            char *p = line + 10;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }

            if (!*p) {
                printf("CMS: Please provide CSV filename.\n");
                continue;
            }

            if (exportToCsvFile(p)) {
                printf("CMS: CSV exported to \"%s\".\n", p);
            } else {
                printf("CMS: Failed to export CSV.\n");
            }
        }

        // EXPORT SQL <file.sql>
        else if (strncmp(upperLine, "EXPORT SQL", 10) == 0) {
            char *p = line + 10;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }

            if (!*p) {
                printf("CMS: Please provide SQL filename.\n");
                continue;
            }

            if (exportToSqlFile(p)) {
                printf("CMS: SQL exported to \"%s\".\n", p);
            } else {
                printf("CMS: Failed to export SQL.\n");
            }
        }

        // IMPORT CSV <file.csv>
        else if (strncmp(upperLine, "IMPORT CSV", 10) == 0) {
            char *p = line + 10;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }

            if (!*p) {
                printf("CMS: Please provide CSV filename.\n");
                continue;
            }

            if (importFromCsvFile(p)) {
                printf("CMS: CSV imported from \"%s\".\n", p);
            } else {
                printf("CMS: Failed to import CSV.\n");
            }
        }

        // FIND NAME "..." or FIND PROGRAMME "..."
        else if (strncmp(upperLine, "FIND NAME", 9) == 0) {
            char *p = line + 9;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            if (*p == '"') {
                p++;
                char *endQuote = strrchr(p, '"');
                if (endQuote) {
                    *endQuote = '\0';
                }
            }
            findStudentsByField("NAME", p);
        }

        else if (strncmp(upperLine, "FIND PROGRAMME", 14) == 0) {
            char *p = line + 14;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            if (*p == '"') {
                p++;
                char *endQuote = strrchr(p, '"');
                if (endQuote) {
                    *endQuote = '\0';
                }
            }
            findStudentsByField("PROGRAMME", p);
        }

        // BACKUP
        else if (strncmp(upperLine, "BACKUP", 6) == 0) {
            if (makeTimestampedBackup()) {
                printf("CMS: Backup file created.\n");
            } else {
                printf("CMS: Backup failed. Please OPEN and SAVE first.\n");
            }
        }

        // Unknown command
        else {
            printf("CMS: Unknown command. Type HELP.\n");
        }
    }
}

// ======================= MAIN FUNCTION ===========================

int main(void)
{
    // find the folder where the .exe is located
    fillProgramDirectoryPath(programDirectoryPath, sizeof(programDirectoryPath));

    // initialize our dynamic student table
    studentTableInit(&studentTable);

    // print declaration
    printDeclaration();

    // prompt user for password before giving access
    if (!checkDatabasePassword()) {
        // if wrong password: cleanup and exit
        studentTableFree(&studentTable);
        return 0;
    }

    printf("Type HELP for available commands.\n\n");

    // run the main interactive command shell
    runCommandShell();

    // free allocated memory before exit
    studentTableFree(&studentTable);

    return 0;
}