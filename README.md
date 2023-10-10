# Getting started
First step to getting started is to clone this repo: 
```sh
git clone --recurse-submodules https://github.com/arjenpdevries/faiss.git
```
Note that `--recurse-submodules` will ensure the correct version of duckdb is pulled allowing you to get started right away.

## Building the extension
To build the extension:
```sh
make
```
The main binaries that will be built are:
```sh
./build/release/duckdb
./build/release/test/unittest
./build/release/extension/faiss/faiss.duckdb_extension
```
- `duckdb` is the binary for the duckdb shell with the extension code automatically loaded. 
- `unittest` is the test runner of duckdb. Again, the extension is already linked into the binary.
- `faiss.duckdb_extension` is the loadable binary as it would be distributed.

## Running the extension
To run the extension code, simply start the shell with `build/release/duckdb`.

Now we can use the features from the extension directly in DuckDB. For example, we can execute a faiss index using the following:
```
D CALL faiss_create('flat8', 8, 'Flat');
┌─────────┐
│ Success │
│ boolean │
├─────────┤
│ 0 rows  │
└─────────┘
```

### Possible values for index type

The index type is passed directly into `index_factory`.
Refer to the following wiki pages for the value of this parameter:

https://github.com/facebookresearch/faiss/wiki/The-index-factory

https://github.com/facebookresearch/faiss/wiki/Faiss-indexes

## Running the tests
Sql test:
```sh
make test
```

Python test:
```sh
make test_python
```

Javascript tests:
```sh
make test_js
```

## Running the conformance/accuracy tests

For now, the accuracy tests are seperate from the normal tests, since they require a large download:

```
wget https://rgw.cs.uwaterloo.ca/pyserini/data/msmarco-passage-openai-ada2.tar -P conformanceTests/
tar xvf conformanceTests/msmarco-passage-openai-ada2.tar -C conformanceTests/
```

Running the conformance tests:

```
make run_msmarco_queries
```

## Distributing your extension
Easy distribution of extensions built with this template is facilitated using a similar process used by DuckDB itself. 
Binaries are generated for various versions/platforms allowing duckdb to automatically install the correct binary.

This step requires that you pass the following 4 parameters to your GitHub repo as action secrets:

| secret name   | description                         |
| ------------- | ----------------------------------- |
| S3_REGION     | s3 region holding your bucket       |
| S3_BUCKET     | the name of the bucket to deploy to |
| S3_DEPLOY_ID  | the S3 key id                       |
| S3_DEPLOY_KEY | the S3 key secret                   |

After setting these variables, all pushes to master will trigger a new (dev) release. Note that your AWS token should
have full permissions to the bucket, and you will need to have ACLs enabled.
