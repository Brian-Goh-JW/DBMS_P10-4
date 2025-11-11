// libraries we will be using (modules)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>

// for windows (file pathing)
#ifdef _WIN32
  #include <windows.h>
  #include <direct.h>
  #define getcwd _getcwd
  #define PATH_SEP '\\'
// for linux/macOS (just in case?)
#else
  #include <unistd.h>
  #include <limits.h>
  #define PATH_SEP '/'
#endif

// constants used available by entire code
#define GROUPNAME "P10-4"
#define NAME_LEN   128
#define PROG_LEN   128
#define START_CAP  128

// a single student row (basically the table row)
typedef struct {
    int   id;
    char  name[NAME_LEN];
    char  programme[PROG_LEN];
    float mark;
} Student;

// a simple dynamic array (meaning resizable)
typedef struct {
    Student *items;   // pointer to heap memory
    size_t   count;   // how many elements used
    size_t   capacity;// how many slots allocated
} Roster;

// the table of students.
static Roster roster = { NULL, 0, 0 };

// a sticky note where we remember the last filename the user opened/saved
static char   lastFile[256] = {0};

// stores the path to the program’s own folder
static char   exeDir[1024]  = {0};


// helper utility section

// trim leading and trailing spaces/newlines/tabs from a string
// delete spaces at the start and end of a line after reading user input or CSV
static void trimSpaces(char *s){
    size_t i=0; while(s[i] && isspace((unsigned char)s[i])) i++;
    if(i) memmove(s, s+i, strlen(s+i)+1);
    int j=(int)strlen(s)-1; while(j>=0 && isspace((unsigned char)s[j])) s[j--]='\0';
}

// case-insensitive equality: returns 1 if strings are equal (tolower).
static int equalsCI(const char *a, const char *b){
    while(*a && *b){ if(tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return 0; a++; b++; }
    return *a=='\0' && *b=='\0';
}

// case-insensitive substring search: returns pointer into hay or NULL
static const char* containsCI(const char *hay, const char *needle){
    if(!*needle) return hay;
    size_t n = strlen(needle);
    for(const char *p=hay; *p; ++p){
        size_t i=0;
        while(i<n && p[i] && tolower((unsigned char)p[i])==tolower((unsigned char)needle[i])) i++;
        if(i==n) return p;
    }
    return NULL;
}

// extract filename stem from a path (e.g., "/a/b/db.txt" -> "db")
static void fileStem(const char *path, char *out, size_t outsz){
    const char *s1=strrchr(path,'/'), *s2=strrchr(path,'\\');
    const char *slash = s1? (s2 && s2>s1 ? s2 : s1) : s2;
    const char *base = slash?slash+1:path;
    const char *dot  = strrchr(base,'.');
    size_t len = dot? (size_t)(dot-base) : strlen(base);
    if(len>=outsz) len=outsz-1;
    memcpy(out, base, len); out[len]='\0';
}

// check if a path is relative or absolute (treats drive-letter or leading slash/backslash as absolute)
static int pathIsRelative(const char *p){
#ifdef _WIN32
    if(strlen(p)>=2 && p[1]==':') return 0; // "C:\..."
    if(p[0]=='\\' || p[0]=='/')   return 0; // "\foo" or "/foo"
    return 1;
#else
    return p[0] != '/';
#endif
}

// build "dir/filename" safely and portably
static void pathJoin(char *out,size_t outsz,const char *dir,const char *file){
    size_t dl=strlen(dir); int need=(dl>0 && dir[dl-1]!=PATH_SEP);
    snprintf(out,outsz, need? "%s%c%s":"%s%s", dir, PATH_SEP, file);
}

// find the directory where the program is running
static void fillExeDir(char *out,size_t outsz){
#ifdef _WIN32
    char buf[1024]; DWORD n=GetModuleFileNameA(NULL,buf,(DWORD)sizeof(buf));
    if(n==0||n>=sizeof(buf)){ out[0]='\0'; return; }
    char *last=strrchr(buf,'\\'); if(last) *last='\0';
    strncpy(out,buf,outsz-1); out[outsz-1]='\0';
#else
    char buf[PATH_MAX]; ssize_t n=readlink("/proc/self/exe",buf,sizeof(buf)-1);
    if(n<=0){ out[0]='\0'; return; }
    buf[n]='\0'; char *last=strrchr(buf,'/'); if(last) *last='\0';
    strncpy(out,buf,outsz-1); out[outsz-1]='\0';
#endif
}

// find the index of a student by id. returns -1 if not found
static int indexById(int id){
    for(size_t i=0;i<roster.count;++i) if(roster.items[i].id==id) return (int)i;
    return -1;
}

// parse key=value from a command line (quotes allowed for values with spaces), returns 1 if found, otherwise 0
static int readKeyValue(const char *src,const char *key,char *out,size_t outlen){
    const char *p=src; size_t klen=strlen(key);
    while((p=containsCI(p,key))){
        if(p!=src && !isspace((unsigned char)p[-1])){ p++; continue; } // ensure key boundary
        const char *eq=p+klen; while(*eq && isspace((unsigned char)*eq)) eq++;
        if(*eq!='='){ p++; continue; }
        eq++; while(*eq && isspace((unsigned char)*eq)) eq++; // skip spaces after '='
        if(*eq=='"'){ // quoted value
            eq++; const char *end=strchr(eq,'"'); if(!end) return 0;
            size_t len=(size_t)(end-eq); if(len>=outlen) len=outlen-1; memcpy(out,eq,len); out[len]='\0'; return 1;
        }else{        // unquoted value (stop at whitespace)
            size_t i=0; while(*eq && !isspace((unsigned char)*eq) && i+1<outlen) out[i++]=*eq++;
            out[i]='\0'; return i>0;
        }
    }
    return 0;
}

// fallback for strcasestr(): if GNU version isn’t available, use our case-insensitive
// containsCI() so strcasestr(...) works portably on all systems
#ifndef _GNU_SOURCE
static const char* strcasestr_fallback(const char *h,const char *n){ return containsCI(h,n); }
#define strcasestr strcasestr_fallback
#endif

// conversion of strings to either int or float
static int parseInt(const char *s,int *out){ char *e=NULL; long v=strtol(s,&e,10); if(s==e) return 0; *out=(int)v; return 1; }
static int parseFloat(const char *s,float *out){ char *e=NULL; float v=strtof(s,&e); if(s==e) return 0; *out=v; return 1; }

// open file for reading: try the given path first; if that fails and it’s a relative path,
// try the same filename next to the program (exeDir). reports which path was used via actualUsed
static FILE* fopenReadSearch(const char *name, const char **actualUsed){
    FILE *fp = fopen(name,"r");
    *actualUsed = name;
    if(!fp && pathIsRelative(name) && exeDir[0]){
        static char alt[1024];
        pathJoin(alt,sizeof(alt),exeDir,name);
        fp = fopen(alt,"r");
        if(fp) *actualUsed = alt;
    }
    return fp;
}

// open a file for writing. if the name is relative, write it next to the EXE;
// otherwise use the path exactly as given. returns FILE* or NULL on failure
static FILE* fopenWriteInExeDir(const char *name, char *actualPath, size_t pathCap){
    if(!name || !name[0]) return NULL;
    if(pathIsRelative(name) && exeDir[0]){
        pathJoin(actualPath, pathCap, exeDir, name);
        return fopen(actualPath, "w");
    }else{
        if(pathCap){ strncpy(actualPath,name,pathCap-1); actualPath[pathCap-1]='\0'; }
        return fopen(name, "w");
    }
}

// set up the resizable array: start with START_CAP slots, zero rows used
static void rosterInit(Roster *r){
    r->capacity = START_CAP;
    r->count    = 0;
    r->items    = (Student*)malloc(r->capacity * sizeof(Student));
    if(!r->items){ fprintf(stderr,"Out of memory\n"); exit(1); }
}

// release the dynamic array and reset counters (safe to call at program end)
static void rosterFree(Roster *r){
    free(r->items); r->items=NULL; r->count=0; r->capacity=0;
}

// ensure there is space for one more Student; if full, double the capacity
static void rosterGrowIfNeeded(Roster *r){
    if(r->count >= r->capacity){
        r->capacity *= 2;
        Student *p = (Student*)realloc(r->items, r->capacity*sizeof(Student));
        if(!p){ fprintf(stderr,"Out of memory\n"); exit(1); }
        r->items = p;
    }
}

// read a TSV database (id<TAB>name<TAB>programme<TAB>mark) into memory.
// returns 1 on success, 0 on open/parse failure (and prints a helpful error).
static int loadRoster(const char *filename){
    const char *used=NULL;
    FILE *fp = fopenReadSearch(filename, &used);        // try user path, else next to EXE
    if(!fp){
        char cwd[1024]; if(!getcwd(cwd,sizeof(cwd))) strncpy(cwd,"<unknown>",sizeof(cwd));
        fprintf(stderr,"CMS: fopen failed for \"%s\": %s\n",filename,strerror(errno));  // show OS error
        fprintf(stderr,"CMS: Current working directory: %s\n",cwd);
        fprintf(stderr,"CMS: Executable directory   : %s\n", exeDir[0]?exeDir:"<unknown>");
        return 0;
    }

    roster.count=0;                                         // start fresh
    char line[1024];
    while(fgets(line,sizeof(line),fp)){                     // read line-by-line
        trimSpaces(line); if(!line[0]) continue;            // skip blank lines
        char *tokId   = strtok(line,"\t");                  // split by tabs: id
        char *tokName = strtok(NULL,"\t");                  // name
        char *tokProg = strtok(NULL,"\t");                  // programme
        char *tokMark = strtok(NULL,"\t");                  // mark
        if(!tokId||!tokName||!tokProg||!tokMark) continue;  // ignore malformed rows

        Student s;                                          // pack into a Student
        s.id = atoi(tokId);
        strncpy(s.name, tokName, NAME_LEN-1); s.name[NAME_LEN-1]='\0';
        strncpy(s.programme, tokProg, PROG_LEN-1); s.programme[PROG_LEN-1]='\0';
        s.mark = (float)atof(tokMark);

        rosterGrowIfNeeded(&roster);                        // expand array if full
        roster.items[roster.count++] = s;                   // append row
    }
    fclose(fp);

    // remember logical name the user typed (so SAVE without arg reuses it)
    strncpy(lastFile, filename, sizeof(lastFile)-1);
    lastFile[sizeof(lastFile)-1]='\0';
    return 1;
}

// save the table as tab-separated text. uses the provided name, or fall back
// to the last OPEN name. relative names are written beside the program
static int saveRoster(const char *filename){
    const char *logical = (filename && filename[0]) ? filename : lastFile;
    if(!logical || !logical[0]) return 0;   // nowhere to save to

    char actualPath[1024];
    FILE *fp = fopenWriteInExeDir(logical, actualPath, sizeof(actualPath));
    if(!fp) return 0;   // open failed

    for(size_t i=0;i<roster.count;++i){
        Student *s=&roster.items[i];
        fprintf(fp,"%d\t%s\t%s\t%.1f\n", s->id, s->name, s->programme, s->mark);    // ID<TAB>Name<TAB>Programme<TAB>Mark
    }
    fclose(fp);
    return 1;
}

// add student function which only adds if ID is new; copy fields safely; grow array if needed
static int addStudent(int id,const char *name,const char *programme,float mark){
    if(indexById(id)!=-1) return 0; // reject duplicate IDs
    Student s; s.id=id; s.mark=mark;
    strncpy(s.name, name?name:"", NAME_LEN-1); s.name[NAME_LEN-1]='\0';
    strncpy(s.programme, programme?programme:"", PROG_LEN-1); s.programme[PROG_LEN-1]='\0';
    rosterGrowIfNeeded(&roster);
    roster.items[roster.count++] = s;
    return 1;
}

// find and return a pointer to the record with this ID (or NULL)
static Student* getStudent(int id){
    int idx=indexById(id); return (idx==-1)?NULL:&roster.items[idx];
}

// update only the fields the user provided; fails if user input ID does not exist
static int editStudent(int id,const char *newName,const char *newProg,const float *newMark){
    int idx=indexById(id); if(idx==-1) return 0;
    if(newName){ strncpy(roster.items[idx].name,newName,NAME_LEN-1); roster.items[idx].name[NAME_LEN-1]='\0'; }
    if(newProg){ strncpy(roster.items[idx].programme,newProg,PROG_LEN-1); roster.items[idx].programme[PROG_LEN-1]='\0'; }
    if(newMark){ roster.items[idx].mark=*newMark; }
    return 1;
}

// function for deleting by shifting everything after the index one slot to the left
static int removeStudent(int id){
    int idx=indexById(id); if(idx==-1) return 0;
    for(size_t i=(size_t)idx+1;i<roster.count;++i) roster.items[i-1]=roster.items[i];
    roster.count--;
    return 1;
}

// what to sort by when SHOW ALL asks for sorting
typedef enum { SORT_NONE, SORT_ID, SORT_MARK } SortField;
// sort direction for the listing (ascending or descending)
typedef enum { ASC, DESC } SortDir;

// comparators for qsort: ascending by ID / ascending by Mark
static int byIdAsc(const void *a,const void *b){ const Student *x=a,*y=b; return (x->id>y->id)-(x->id<y->id); }
static int byMarkAsc(const void *a,const void *b){ const Student *x=a,*y=b; return (x->mark>y->mark)-(x->mark<y->mark); }

// prints the whole table, optionally sorted by a field and direction
static void showAll(SortField field, SortDir dir){
    // make a temporary copy so we can sort without touching the real data
    Student *copy=(Student*)malloc(roster.count*sizeof(Student)); if(!copy){ fprintf(stderr,"OOM\n"); return; }
    for(size_t i=0;i<roster.count;++i) copy[i]=roster.items[i];

    // sort the copy if a sort was asked for
    if(field==SORT_ID)       qsort(copy,roster.count,sizeof(Student),byIdAsc);
    else if(field==SORT_MARK)qsort(copy,roster.count,sizeof(Student),byMarkAsc);

    // if DESC was requested, reverse the sorted array in-place
    if(dir==DESC){
        for(size_t i=0;i<roster.count/2;++i){ Student t=copy[i]; copy[i]=copy[roster.count-1-i]; copy[roster.count-1-i]=t; }
    }

    // print header then each row
    printf("CMS: Here are all the records found in the table \"StudentRecords\".\n");
    printf("ID  Name  Programme  Mark\n");
    for(size_t i=0;i<roster.count;++i)
        printf("%d %s %s %.1f\n", copy[i].id, copy[i].name, copy[i].programme, copy[i].mark);

    free(copy); // clean up the temporary copy
}

// prints summary about the current table (count, avg, highest, lowest)
static void showSummary(void){
    if(roster.count==0){ printf("CMS: No records loaded.\n"); return; } // nothing to summarise
    size_t n=roster.count; float sum=0.0f;                              // running totals
    float lo=roster.items[0].mark, hi=roster.items[0].mark; size_t ilo=0, ihi=0;    // track min/max and who owns them

    for(size_t i=0;i<n;++i){
        float m=roster.items[i].mark; sum+=m;   // add to average
        if(m<lo){ lo=m; ilo=i; } if(m>hi){ hi=m; ihi=i; }   // update lowest/highest when seen
    }

    // print the summary
    printf("CMS: SUMMARY\n");
    printf("Total students: %zu\n", n);
    printf("Average mark: %.2f\n", sum/(float)n);
    printf("Highest: %.1f (%s)\n", roster.items[ihi].mark, roster.items[ihi].name);
    printf("Lowest : %.1f (%s)\n", roster.items[ilo].mark, roster.items[ilo].name);
}

// split one CSV line into 4 fields (ID, Name, Programme, Mark)
// handles quoted fields (with \"\" for a quote inside) and trims spaces around commas
static int csvSplit4(const char *line,
                     char *f0,size_t n0,
                     char *f1,size_t n1,
                     char *f2,size_t n2,
                     char *f3,size_t n3)
{
    const char *p = line;               // reader cursor
    char *outs[4] = {f0,f1,f2,f3};      // where to put each field
    size_t caps[4]= {n0,n1,n2,n3};      // max sizes for each output buffer

    for(int col=0; col<4; ++col){
        // skip spaces/tabs before the next field
        while(*p==' ' || *p=='\t') p++;

        // if the field starts with a quote, read until the closing quote
        if(*p=='"'){
            p++; // skip opening quote
            size_t k=0;
            while(*p){
                if(*p=='"'){
                    if(*(p+1)=='"'){ // escaped quote
                        if(k+1 < caps[col]) outs[col][k++] = '"';
                        p+=2;
                    }else{ // end quote
                        p++;
                        // skip spaces right after the quote, then expect comma (except last field)
                        while(*p==' '||*p=='\t') p++;
                        if(col<3){ if(*p!=',') return 0; p++; }
                        break;
                    }
                }else{  // normal character inside quotes
                    if(k+1 < caps[col]) outs[col][k++] = *p;
                    p++;
                }
            }
            outs[col][k] = '\0';    // terminate this field
        }else{
            // unquoted: read until comma or end
            size_t k=0;
            while(*p && *p!=','
                   && *p!='\r' && *p!='\n'){
                if(k+1 < caps[col]) outs[col][k++] = *p;
                p++;
            }
            outs[col][k]='\0';
            if(col<3){                  // missing comma between fields
                if(*p!=',') return 0;   // skip comma
                p++;
            }
        }
    }

    // ignore any trailing whitespace/newlines after the 4th field
    while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    // if there is still text, that means extra columns -> treat as error
    if(*p!='\0') return 0;

    return 1;
}

// export the current roster to a CSV file placed next to the .exe
static int exportCsv(const char *csvName){
    if(!csvName||!csvName[0]) return 0;     // need a filename

    char actual[1024];
    FILE *fp = fopenWriteInExeDir(csvName, actual, sizeof(actual));     // open "<exe folder>/<csvName>" for write
    if(!fp) return 0;

    fprintf(fp,"ID,Name,Programme,Mark\n");     // CSV header
    for(size_t i=0;i<roster.count;++i){
        const Student *s=&roster.items[i];
        // write: id,"name","programme",mark
        // if a quote appears inside a field, double it so CSV stays valid
        fprintf(fp,"%d,\"", s->id);         // start id + opening quote for name
        for(const char *c=s->name; *c; ++c){ if(*c=='"') fputc('"',fp); fputc(*c,fp); }
        fprintf(fp,"\",\"");                // close name, open programme
        // Programme
        for(const char *c=s->programme; *c; ++c){ if(*c=='"') fputc('"',fp); fputc(*c,fp); }
        fprintf(fp,"\",%.1f\n", s->mark);   // close programme and print mark
    }
    fclose(fp);
    return 1;
}

// export the current roster as SQL: DROP/CREATE table + INSERT rows (file saved next to the .exe)
static int exportSql(const char *sqlName){
    if(!sqlName||!sqlName[0]) return 0;     // need a filename

    char actual[1024];
    FILE *fp = fopenWriteInExeDir(sqlName, actual, sizeof(actual));     // open "<exe folder>/<sqlName>" for write
    if(!fp) return 0;
    
    // emit schema and table definition
    fprintf(fp,"-- SQL dump generated by CMS\n");
    fprintf(fp,"DROP TABLE IF EXISTS StudentRecords;\n");
    fprintf(fp,"CREATE TABLE StudentRecords (\n"
               "  id INTEGER PRIMARY KEY,\n"
               "  name TEXT NOT NULL,\n"
               "  programme TEXT NOT NULL,\n"
               "  mark REAL NOT NULL\n"
               ");\n");
    
    // emit one INSERT per student (escape single quotes inside text fields)
    for(size_t i=0;i<roster.count;++i){
        const Student *s=&roster.items[i];
        // escape single quotes for SQL
        char qName[2*NAME_LEN]={0}, qProg[2*PROG_LEN]={0};      // buffers with quotes escaped
        // copy name -> qName, duplicating any ' as '' for SQL safety
        size_t k=0;
        for(size_t j=0;j<strlen(s->name)&&k+2<sizeof(qName);++j){ if(s->name[j]=='\''){ qName[k++]='\''; qName[k++]='\''; } else qName[k++]=s->name[j]; }
        // copy programme -> qProg, same escaping
        k=0;
        for(size_t j=0;j<strlen(s->programme)&&k+2<sizeof(qProg);++j){ if(s->programme[j]=='\''){ qProg[k++]='\''; qProg[k++]='\''; } else qProg[k++]=s->programme[j]; }

        fprintf(fp,"INSERT INTO StudentRecords(id,name,programme,mark) "
                   "VALUES(%d,'%s','%s',%.1f);\n",
                s->id, qName, qProg, s->mark);
    }
    fclose(fp);
    return 1;
}

// import students from a CSV file (handles header row, quoted fields, and skips duplicates)
static int importCsv(const char *csvName){
    // try as-typed; if relative, also try next to the program (.exe) folder
    const char *used=NULL;
    FILE *fp = fopenReadSearch(csvName, &used);
    if(!fp) return 0;

    char line[2048];

    // read first line: it might be a header or the first data row
    if(fgets(line,sizeof(line),fp)==NULL){ fclose(fp); return 0; }

    // check if the first line is a header. if it is, skip it.
    // if it's not, we'll process it as data.
    {
        char tmp[2048]; strncpy(tmp,line,sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
        trimSpaces(tmp);
        if(tmp[0] != '\0'){
            char f0[64], f1[NAME_LEN], f2[PROG_LEN], f3[64];
            if(csvSplit4(tmp, f0,sizeof(f0), f1,sizeof(f1), f2,sizeof(f2), f3,sizeof(f3))){
                // if all four fields match typical header names (case-insensitive),
                // read the next line as the first data row.
                // basically, if line looks like "ID,Name,Programme,Mark" (case-insensitive), skip it
                if(equalsCI(f0,"ID") && equalsCI(f1,"Name") && equalsCI(f2,"Programme") && equalsCI(f3,"Mark")){
                    // do nothing; header consumed, move on
                } else {
                    // not a header -> treat this first line as data
                    // first line is real data -> add it (avoid duplicate IDs)
                    int   id   = atoi(f0);
                    float mark = (float)atof(f3);
                    if(indexById(id)==-1) addStudent(id, f1, f2, mark);
                }
            }
        }
    }

    // read the rest of the file line by line
    while(fgets(line,sizeof(line),fp)!=NULL){
        char trimmed[2048]; strncpy(trimmed,line,sizeof(trimmed)-1); trimmed[sizeof(trimmed)-1]='\0';
        trimSpaces(trimmed);
        if(trimmed[0]=='\0') continue;      // skip blank lines

        char f0[64], f1[NAME_LEN], f2[PROG_LEN], f3[64];
        if(!csvSplit4(trimmed, f0,sizeof(f0), f1,sizeof(f1), f2,sizeof(f2), f3,sizeof(f3))) continue;

        // extra guard: basiccally, if someone repeats the header mid-file, skip it.
        if(equalsCI(f0,"ID") && equalsCI(f1,"Name") && equalsCI(f2,"Programme") && equalsCI(f3,"Mark")) continue;

        int   id   = atoi(f0);
        float mark = (float)atof(f3);
        if(indexById(id)==-1) addStudent(id, f1, f2, mark);     // insert only if ID not present
    }

    fclose(fp);
    return 1;
}

// simple case-insensitive search and print helper by either Name or Programme
static void findLike(const char *field,const char *needle){
    if(!needle||!needle[0]){ printf("CMS: Please provide a search string.\n"); return; }
    printf("CMS: Search results for %s contains \"%s\":\n", field, needle);
    printf("ID  Name  Programme  Mark\n");
    int hits=0;
    for(size_t i=0;i<roster.count;++i){
        if(equalsCI(field,"NAME") && containsCI(roster.items[i].name,needle)){
            printf("%d %s %s %.1f\n",roster.items[i].id,roster.items[i].name,roster.items[i].programme,roster.items[i].mark); hits++;
        }else if(equalsCI(field,"PROGRAMME") && containsCI(roster.items[i].programme,needle)){
            printf("%d %s %s %.1f\n",roster.items[i].id,roster.items[i].name,roster.items[i].programme,roster.items[i].mark); hits++;
        }
    }
    if(!hits) printf("(no matches)\n");
}

// makes a timestamped backup next to the current data file (e.g., db.bak-YYYYMMDD-HHMMSS.txt)
static int makeBackup(void){
    if(!lastFile[0]) return 0;      // need a current/last filename
    char stem[256]; fileStem(lastFile,stem,sizeof(stem));       // get base name without extension

    time_t t=time(NULL); struct tm *tm=localtime(&t);
    char ts[64]; strftime(ts,sizeof(ts),"%Y%m%d-%H%M%S",tm);        // build time tag

    char outName[512]; snprintf(outName,sizeof(outName),"%s.bak-%s.txt",stem,ts);
    return saveRoster(outName);     // reuse normal save to write backup
}

// declaration requirement?
static void printDeclaration(void){
    time_t t=time(NULL); struct tm *tm=localtime(&t); char date[32]; strftime(date,sizeof(date),"%Y-%m-%d",tm);
    printf("\nOur Declaration Section\n");
    printf("SIT's policy on copying does not allow the students to copy source code as well as assessment solutions from another person AI or other places. It is the students' responsibility to guarantee that their assessment solutions are their own work. Meanwhile, the students must also ensure that their work is not accessible by others. Where such plagiarism is detected, both of the assessments involved will receive ZERO mark.\n\n");
    printf("We hereby declare that:\n");
    printf("We fully understand and agree to the abovementioned plagiarism policy.\n");
    printf("We did not copy any code from others or from other places.\n");
    printf("We did not share our codes with others or upload to any other places for public access and will not do that in the future.\n");
    printf("We agree that our project will receive Zero mark if there is any plagiarism detected.\n");
    printf("We agree that we will not disclose any information or material of the group project to others or upload to any other places for public access.\n");
    printf("We agree that we did not copy any code directly from AI generated sources.\n\n");
    printf("Declared by: %s\n", GROUPNAME);
    printf("Team members:\n1. BRIAN GOH JUN WEI\n2. HAN YONG\n3. JERREL\n4. KENDRICK\n5. XIAN YANG\n");
    printf("Date: %s\n\n", date);
}

// when user inputs HELP:
// basically the UI / guide for user
static void printHelp(void){
    puts("Commands (examples included):\n");

    puts("OPEN / SAVE");
    puts("  OPEN <file>                 e.g.  OPEN db.txt");
    puts("  SAVE                        (saves back to last OPEN file)");
    puts("  SAVE <file>                 e.g.  SAVE test.txt\n");

    puts("VIEW");
    puts("  SHOW ALL                    list all rows");
    puts("  SHOW ALL SORT BY ID ASC     or DESC");
    puts("  SHOW ALL SORT BY MARK ASC   or DESC");
    puts("  SHOW SUMMARY                show count/average/highest/lowest\n");

    puts("ADD / LOOKUP / EDIT / REMOVE");
    puts("  INSERT ID=<int> Name=\"...\" Programme=\"...\" Mark=<float>");
    puts("    e.g. INSERT ID=2501001 Name=\"Brian Goh\" Programme=\"Digital Supply Chain\" Mark=88.8");
    puts("  QUERY ID=<int>              e.g. QUERY ID=2501066");
    puts("  UPDATE ID=<int> [Name=...] [Programme=...] [Mark=<float>]");
    puts("    e.g. UPDATE ID=2501001 Programme=\"Game Development\" Mark=95.5");
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

// interactive command loop: reads a line, figures out which command it is, and runs it
static void runShell(void){
    char line[1024];
    while(1){
        printf(GROUPNAME ": ");                     // show prompt like "P10-4: " our groupname to be displayed
        if(!fgets(line,sizeof(line),stdin)) break;  // read a line; stop if input closes (Ctrl+D/Ctrl+Z)
        trimSpaces(line); if(!line[0]) continue;    // remove extra spaces; ignore empty line

        // makes an UPPERCASE copy for easy command matching, while keeping original for arguments
        char up[1024]; strncpy(up,line,sizeof(up)-1); up[sizeof(up)-1]='\0';
        for(size_t i=0;i<strlen(up);++i) up[i]=(char)toupper((unsigned char)up[i]);

        // EXIT / QUIT: leave the loop
        if(equalsCI(up,"EXIT")||equalsCI(up,"QUIT")) break;

        // HELP: show the command list
        else if(strncmp(up,"HELP",4)==0){ printHelp(); }

        // OPEN <file>: load TSV database
        else if(strncmp(up,"OPEN",4)==0){
            char *p=line+4; while(*p && isspace((unsigned char)*p)) p++;
            if(!*p){ printf("CMS: Please provide a filename.\n"); continue; }
            char *fname=p; if(*p=='"'){ p++; char *e=strrchr(p,'"'); if(e) *e='\0'; fname=p; }
            if(loadRoster(fname)) printf("CMS: The database file \"%s\" is successfully opened.\n",fname);
            else                  printf("CMS: Failed to open file \"%s\".\n",fname);
        }

        // SAVE [file]: write TSV (default to last OPEN file if no name given)
        else if(strncmp(up,"SAVE",4)==0){
            char *p=line+4; while(*p && isspace((unsigned char)*p)) p++;
            const char *fname=*p? p: NULL;      // NULL means reuse lastFile
            if(saveRoster(fname)) printf("CMS: Data successfully saved.\n");
            else                  printf("CMS: Failed to save. Please OPEN a file first or provide a filename.\n");
        }

        // SHOW ALL [SORT BY ID|MARK ASC|DESC]: print table (optionally sorted)
        else if(strncmp(up,"SHOW ALL",8)==0){
            SortField f= SORT_NONE; SortDir d=ASC;
            if(strstr(up,"SORT BY ID"))     { f=SORT_ID;   d=strstr(up,"DESC")?DESC:ASC; }
            else if(strstr(up,"SORT BY MARK")){ f=SORT_MARK; d=strstr(up,"DESC")?DESC:ASC; }
            showAll(f,d);
        }

        // INSERT ID=... Name="..." Programme="..." Mark=...
        else if(strncmp(up,"INSERT",6)==0){
            char idb[64]="", nb[NAME_LEN]="", pb[PROG_LEN]="", mb[64]="";
            if(!readKeyValue(line,"ID",idb,sizeof idb)){ printf("CMS: Missing ID=\n"); continue; }
            if(!readKeyValue(line,"Name",nb,sizeof nb)){ printf("CMS: Missing Name=\n"); continue; }
            if(!readKeyValue(line,"Programme",pb,sizeof pb)){ printf("CMS: Missing Programme=\n"); continue; }
            if(!readKeyValue(line,"Mark",mb,sizeof mb)){ printf("CMS: Missing Mark=\n"); continue; }
            int id; float mk; if(!parseInt(idb,&id)){ printf("CMS: Invalid ID.\n"); continue; }
            if(!parseFloat(mb,&mk)){ printf("CMS: Invalid Mark.\n"); continue; }
            if(addStudent(id,nb,pb,mk)) printf("CMS: A new record with ID=%d is successfully inserted.\n",id);
            else                        printf("CMS: The record with ID=%d already exists.\n",id);
        }

        // QUERY ID=... : show a single record
        else if(strncmp(up,"QUERY",5)==0){
            char idb[64]=""; if(!readKeyValue(line,"ID",idb,sizeof idb)){ printf("CMS: Missing ID=\n"); continue; }
            int id; if(!parseInt(idb,&id)){ printf("CMS: Invalid ID.\n"); continue; }
            Student *s=getStudent(id);
            if(!s) printf("CMS: The record with ID=%d does not exist.\n",id);
            else {
                printf("CMS: The record with ID=%d is found in the data table.\n",id);
                printf("ID  Name  Programme  Mark\n");
                printf("%d %s %s %.1f\n",s->id,s->name,s->programme,s->mark);
            }
        }

        // UPDATE ID=... [Name=...] [Programme=...] [Mark=...] (the edit part)
        else if(strncmp(up,"UPDATE",6)==0){
            char idb[64]="", nb[NAME_LEN]="", pb[PROG_LEN]="", mb[64]="";
            if(!readKeyValue(line,"ID",idb,sizeof idb)){ printf("CMS: Missing ID=\n"); continue; }
            int id; if(!parseInt(idb,&id)){ printf("CMS: Invalid ID.\n"); continue; }
            int hasName=readKeyValue(line,"Name",nb,sizeof nb);
            int hasProg=readKeyValue(line,"Programme",pb,sizeof pb);
            int hasMark=readKeyValue(line,"Mark",mb,sizeof mb);
            float mk; float *pm=NULL; if(hasMark){ if(!parseFloat(mb,&mk)){ printf("CMS: Invalid Mark.\n"); continue; } pm=&mk; }
            if(editStudent(id, hasName?nb:NULL, hasProg?pb:NULL, pm)) printf("CMS: The record with ID=%d is successfully updated.\n",id);
            else                                                      printf("CMS: The record with ID=%d does not exist.\n",id);
        }

        // DELETE ID=... : confirm before removing
        else if(strncmp(up,"DELETE",6)==0){
            char idb[64]=""; if(!readKeyValue(line,"ID",idb,sizeof idb)){ printf("CMS: Missing ID=\n"); continue; }
            int id; if(!parseInt(idb,&id)){ printf("CMS: Invalid ID.\n"); continue; }
            if(indexById(id)==-1){ printf("CMS: The record with ID=%d does not exist.\n",id); continue; }
            printf("CMS: Type Y to Confirm or N to cancel: ");
            char yn[16]; if(!fgets(yn,sizeof(yn),stdin)){ printf("\n"); continue; } trimSpaces(yn);
            if(yn[0]=='Y'||yn[0]=='y'){
                if(removeStudent(id)) printf("CMS: The record with ID=%d is successfully deleted.\n",id);
                else                  printf("CMS: Delete failed.\n");
            }else{
                printf("CMS: Delete cancelled.\n");
            }
        }

        // SHOW SUMMARY: print count/average/highest/lowest
        else if(strncmp(up,"SHOW SUMMARY",12)==0){ showSummary(); }

        // EXPORT CSV <file.csv>
        else if(strncmp(up,"EXPORT CSV",10)==0){
            char *p=line+10; while(*p && isspace((unsigned char)*p)) p++;
            if(!*p){ printf("CMS: Please provide CSV filename.\n"); continue; }
            if(exportCsv(p)) printf("CMS: CSV exported to \"%s\".\n",p);
            else             printf("CMS: Failed to export CSV.\n");
        }

        // EXPORT SQL <file.sql>
        else if(strncmp(up,"EXPORT SQL",10)==0){
            char *p=line+10; while(*p && isspace((unsigned char)*p)) p++;
            if(!*p){ printf("CMS: Please provide SQL filename.\n"); continue; }
            if(exportSql(p)) printf("CMS: SQL exported to \"%s\".\n",p);
            else             printf("CMS: Failed to export SQL.\n");
        }

        // IMPORT CSV <file.csv>
        else if(strncmp(up,"IMPORT CSV",10)==0){
            char *p=line+10; while(*p && isspace((unsigned char)*p)) p++;
            if(!*p){ printf("CMS: Please provide CSV filename.\n"); continue; }
            if(importCsv(p)) printf("CMS: CSV imported from \"%s\".\n",p);
            else             printf("CMS: Failed to import CSV.\n");
        }

        // FIND NAME "..."  or  FIND PROGRAMME "..."
        else if(strncmp(up,"FIND NAME",9)==0){
            char *p=line+9; while(*p && isspace((unsigned char)*p)) p++;
            if(*p=='"'){ p++; char *e=strrchr(p,'"'); if(e) *e='\0'; }      // strip surrounding quotes
            findLike("NAME",p);
        }

        else if(strncmp(up,"FIND PROGRAMME",14)==0){
            char *p=line+14; while(*p && isspace((unsigned char)*p)) p++;
            if(*p=='"'){ p++; char *e=strrchr(p,'"'); if(e) *e='\0'; }      // strip surrounding quotes
            findLike("PROGRAMME",p);
        }

        // BACKUP: write a timestamped copy next to the current file
        else if(strncmp(up,"BACKUP",6)==0){
            if(makeBackup()) printf("CMS: Backup file created.\n");
            else             printf("CMS: Backup failed. Please OPEN and SAVE first.\n");
        }

        // anything else inputted: unknown command pops
        else { printf("CMS: Unknown command. Type HELP.\n"); }
    }
}

// main function: set folders, set up memory, show intro, run the command loop, then clean up
int main(void){
    fillExeDir(exeDir, sizeof(exeDir));  // so writes land next to EXE for relative names
    rosterInit(&roster);
    printDeclaration();
    printf("Type HELP for available commands.\n\n");
    runShell();
    rosterFree(&roster);
    return 0;
}