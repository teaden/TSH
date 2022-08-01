#TSH.c

This program is a shell that includes the following functionality:
•	Sequencing with waiting for or backgrounding commands (with ; and & operators)
•	Input and output file redirection (with <, >, and >> operators)
•	Piping (with | operator)

Usage:

Compile: gcc -o TSH TSH.c 
Execute: ./TSH

The following examples represent a small sample of the variations of acceptable commands:
•	ls ; pwd &
•	wc < input_file > output_file
•	ls | wc
•	cat input_file | grep while | wc >> output_file
•	cd ~/Project_4 ; ls
•	:

Undefined Behavior:

wc < input_a < input_b >  output_ a >> output_b
•	Utilizes the rightmost input redirector and rightmost output redirector

ls <
ls >
ls >>
•	Outputs error message if no file is specified after a redirection operator


ls >>>><< input_file
wc >>> input_file
•	Uses the rightmost accepted redirection operator (<, >, >>)

;;
: & ls
•	Allows empty commands or the bash null command to be sequenced

