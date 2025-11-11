DBMS PROJECT - README
You can open/save a text file, insert/update/delete/search students, import/export CSV, dump SQL, and make timestamped backups — all from a simple prompt.


How do I run it?
Windows
Open a terminal in this folder.

Build:
gcc project.c -o project.exe

Run:
.\project.exe


macOS / Linux
Open a terminal in this folder.

Build:
cc project.c -o project

Run:
./project


On startup, you will see:
Our school's declaration paragraph;
Then you’ll see the prompt:
P10-4:
^ Type commands here. Use HELP to view the available commands.


QUICK START (short check, copy paste one by one):
OPEN db.txt
SHOW ALL
INSERT ID=2501234 Name="Jerrel" Programme="Information Security" Mark=84.8
INSERT ID=2502345 Name="Han Yong" Programme="Data Science" Mark=98.4
SHOW ALL SORT BY MARK DESC
SHOW SUMMARY
SAVE
BACKUP
EXIT



If not, here is some guidance:

OPEN <file>
Load a file (e.g., OPEN db.txt).

SAVE
Save back to the last opened file.

SAVE <file>
Save to a new file and remember that name (e.g., SAVE classA.txt).


SHOW ALL
List everything. You can sort:

SHOW ALL SORT BY ID ASC

SHOW ALL SORT BY MARK DESC

SHOW SUMMARY
Show total, average, highest, lowest.


INSERT …
Add a student:
INSERT ID=2501066 Name="Brian Goh" Programme="DSC" Mark=90.4

QUERY ID=<id>
Show one student: QUERY ID=2501066

UPDATE ID=…
Change fields:
UPDATE ID=2501066 Programme="Digital Supply Chain" Mark=88.8

DELETE ID=<id>
Remove a student (you’ll be asked to type Y to confirm).

FIND NAME "text" / FIND PROGRAMME "text"
Case-insensitive search (e.g., FIND NAME "brian").

IMPORT CSV <file.csv>
Read a CSV (see format below). The header is skipped automatically.

EXPORT CSV <file.csv>
Write a CSV for Excel/Sheets.

EXPORT SQL <file.sql>
Write SQL INSERT statements you can use in a database.

BACKUP
Make a copy like db.bak-YYYYMMDD-HHMMSS.txt.

HELP / EXIT



TO NOTE!!!
CSV rules (important):

First line (header) must be exactly:
ID,Name,Programme,Mark

If a name has commas or quotes, put it in quotes and double any inner quotes:
"O'Brian, Connor" → CSV stores it as "O''Brian, Connor" for SQL safety.


Where do files get saved?
If you give just a name (like SAVE roster.txt), the file is written next to the program (project.exe on Windows or ./project on macOS/Linux).



Common problems (and quick fixes)!!!!:
“Failed to open file”
Check the path. Try OPEN ./db.txt (macOS/Linux) or OPEN .\db.txt (Windows).
The program also tries the folder where the app itself lives.

Weird “0 Name Programme 0.0” after import
Usually a bad CSV line or a duplicated header. Remove blank lines and make sure each row has exactly 4 fields.

I saved but can’t find the file
Look in the same folder as the app. Relative names save beside the executable.


That is about it. Thanks for trying, prof, or any other people (:
Build it, run it, and use the sample session to get a feel for it.
