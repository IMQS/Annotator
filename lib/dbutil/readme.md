# DBUtil

DBUtil is a library that is intended to house a collection of IMQS-specific database utility functions.
This is for things that are at a higher level of abstraction than dba. We keep this separation, so that
we can remember that dba is strictly a database abstraction system, but things that are very IMQS specific
go into DBUtil. 

This is a static library, so no global state is allowed in here.