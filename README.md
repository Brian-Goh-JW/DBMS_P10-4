Class Management System (CMS) (Our DBMS Project)

Can open/save a text database, insert/update/delete/search students, import/export CSV, dump SQL, and make timestamped backups — all from a simple command prompt

Features
Core commands

OPEN / SAVE – Load and save a text database file (TSV format).
INSERT / UPDATE / DELETE – Add, edit, and remove student records.
QUERY – Search for a student by ID.
SHOW ALL – List all students currently in memory.
SHOW SUMMARY – Show total students, average mark, highest and lowest marks.


Enhancement features

Sorting:
SHOW ALL SORT BY ID ASC|DESC
SHOW ALL SORT BY MARK ASC|DESC

Searching:
FIND NAME "text"
FIND PROGRAMME "text"

CSV import/export:
IMPORT CSV <file.csv>
EXPORT CSV <file.csv>

SQL export:
EXPORT SQL <file.sql>

Backup:
BACKUP (creates <stem>.bak-YYYYMMDD-HHMMSS.txt next to your DB file)

Unique feature
Database password: On startup, the program asks for a password before any command can be used.
Default password: password
3 attempts only — after 3 wrong tries, the program exits.


HOW TO RUN?
For windows:
# Build
gcc project.c -o project.exe

# Run
.\project.exe

For macOS / Linux:
# Build
cc project.c -o project

# Run
./project

What happens on startup?
It will ask for the database password:
Please enter database password to continue (attempt 1 of 3):
Enter: password

After a correct password, you’ll see the prompt:
P10-4:

Type commands at this prompt. Use HELP to see all available commands.

The following are sample copy paste to save time:
OPEN db.txt
SHOW ALL

INSERT ID=2501234 Name="Jerrel" Programme="Information Security" Mark=84.8
INSERT ID=2502345 Name="Han Yong" Programme="Data Science" Mark=98.4

SHOW ALL SORT BY MARK DESC
SHOW SUMMARY

SAVE
BACKUP
EXIT
