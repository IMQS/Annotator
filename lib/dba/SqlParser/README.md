# SQL Parser Modifications:

For editing and using the SQL Parser the following process can be used:
1. Open Coco/Coco.sln to modify the Parser Generator if it is necessary (which in most cases it will not be). 
   The Compiler Generator is a 3rd Party library based upon Coco/R. 
   See the [Coco Website](http://www.ssw.uni-linz.ac.at/Coco/) for more info.
2. SQLParser.ATG contains most of the crucial code for this project. Change this to change how the Abstract Syntax Tree is built as well as how the grammar is specified.
   It specifies a Grammar for the SQL WHERE clause complete with token and production definitions. 
   You'll notice that each production has C++ code mixed with it; this would be the semantic actions that is responsible for building the Abstract Syntax Tree for quick future evaluation.
   For more info on how all of this Coco/R stuff works, there's a great [tutorial](http://www.ssw.uni-linz.ac.at/Coco/Tutorial/)
3. Next you'd run coco.bat which invokes the parser generator (see 1) which analysis the SQLParser.ATG (see 2) and finally produces code for a Scanner and a Parser which is dropped in the Generated folder.
4. If you need to make changes to the operations of either the scanner or parser you can edit the frame files for both which are located in this folder. 
   (The compiler generator (see 1) uses these frames as templates to generate the actual Parser and Scanner).