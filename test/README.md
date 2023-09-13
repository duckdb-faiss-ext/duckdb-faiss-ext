# Testing the faiss extension
This directory contains all the tests for the faiss extension. The `sql` directory holds tests that are written as [SQLLogicTests](https://duckdb.org/dev/sqllogictest/intro.html).

The root makefile contains targets to build and run all of these sql tests. To run the SQLLogicTests:
```bash
make test
```

To run the python tests:
```sql
make test_python
```

For other client tests check the makefile in the root of this repository.