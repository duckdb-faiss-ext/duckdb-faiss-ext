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

## Running the conformance/accuracy tests

For now, the accuracy tests are seperate from the normal tests, since they require a large download:

```
wget https://rgw.cs.uwaterloo.ca/pyserini/data/msmarco-passage-openai-ada2.tar -P conformanceTests/
tar xvf conformanceTests/msmarco-passage-openai-ada2.tar -C conformanceTests/
```
_hello_
Running the conformance tests:

```
make run_msmarco_queries
```

# Functions

The faiss extension provides several functions that can be used to interact with faiss. These are split into a couple general catagories: Creation/deletion, adding, and searching.
You can see the all these functions below

## Creation/Deletion

These functions allow you to do all kinds of things with indexes, without modifying the data.

### faiss\_create

```sql
CALL faiss_create(string name, int dimension, string index_type);
```

 - `name`: The name given to the index. Each database can only have a single index per name, this function will crash when you give it a name with an index already attached. These are global, and can be refered to back later.
 - `dimension`: The dimensionality of the data.
 - `index_type`: The index type given to [the faiss index factory](https://github.com/facebookresearch/faiss/wiki/The-index-factory). 

### faiss\_create\_params

```sql
CALL faiss_create_params(string name, int dimension, string index_type, MAP<string, string> parameters);
```

 - `name`: The name given to the index. Each database can only have a single index per name, this function will crash when you give it a name with an index already attached. These are global, and can be refered to back later.
 - `dimension`: The dimensionality of the data.
 - `index_type`: The index type given to [the faiss index factory](https://github.com/facebookresearch/faiss/wiki/The-index-factory). 
 - `parameters`: The parameters of the index. For example passing `{'efConstruction': '1000'}` when creating an `HNSW` index, will set the efConstruction field to 1000. This is recursive, for example, when using an `IVF` with `HNSW`, `ivf.efConstruction` can be used to set the `efConstruction` value on the `HNSW` index. Note that currently, the implementation is verry limmited. Recursion is only implemented for `IDMap`, and only `efConstruction` is implemented for HNSW. Other than that, no other parameters are implemented. This is because I did not need these for my thesis, but should be easy to add.

### faiss\_save

```sql
CALL faiss_save(string name, string path);
```

 - `name`: The name of the index to be saves.
 - `path`: The target path to be saved to.

Note that saving/loading an index, may remove the ability to add data to it or train it. Only if the index is still untrained, it it not mutable. This is because if the index is trained, we do not know the training data. In the future this restriction might be eliminated when using the manual train function.


### faiss\_load

```sql
CALL faiss_save(string name, string path);
```

 - `name`: The name given to the loaded index
 - `path`: The location of the index to be read.

Note that saving/loading an index, may remove the ability to add data to it or train it. Only if the index is still untrained, it it not mutable. This is because if the index is trained, we do not know the training data. In the future this restriction might be eliminated when using the manual train function.

### faiss\_destroy

```sql
CALL faiss_destroy(string name, string path);
```

 - `name`: The name of the index to be destroyed

## Training and Adding

### faiss\_add

```sql
CALL faiss_add(TABLE data, string name);
```

 - `data`: A table with one or 2 columns. If there is only one column, this is the data column, and ids are creatd by faiss if possible. If there are 2 columns, the first is the id column of an integer type, and the second column is the data column. The data column most be a list of length equal to the index dimension
 - `name`: The name of the index that should be added to.

`faiss_add` always retrains the index, and keeps a copy of all the data for later retraining, unless previously trained with `faiss_manual_train`. If retraining in undesired, or if memory usage is important, use `faiss_manual_train` for more control over training.

### faiss\_manual\_train

```sql
CALL faiss_manual_train(TABLE data, string name);
```

 - `data`: A table with a single column. The type of this column must be a list of length equal to the index dimension.
 - `name`: The name of the index that should be added to.

`faiss_manual_train` trains the index, and flags this index to be manually trained. This means that calls to `faiss_add` will not train the index, and do not store copies of the data. This saves on retraining when adding later, and a lot of memory for larger indices.

## Searching and filtering

### faiss\_search

```sql
CALL faiss_search(string name, integer k, List q);
```

 - `name`: The name of the index that should be added to.
 - `k`: The amount of results to be returned.
 - `q`: The query vector for which the nearest neighbors should be computer.

And a variant with parameters:

```sql
CALL faiss_search(string name, integer k, List q, MAP<string, string> parameters);
```

 - `parameters`: The parameters for searching the index. For example passing `{'efSearch': '1000'}` when searching`HNSW` index, will set the efSearch field to 1000. This is recursive, for example, when using an `IVF` with `HNSW`, `ivf.efSearch` can be used to set the `efSearch` value for the `HNSW` index. Note that currently, the implementation is verry limmited. Recursion is only implemented for `IDMap` and `IVF`, and only very few fields are implemented. This is because I did not need these for my thesis, but should be easy to add.

`faiss_search` returns a list of structs with 3 fields: `rank` of type `INTEGER`, `label` of type `BIGINT`, `distance` of type `DISTANCE`. The length of this list is always `k`, even if not enough results were found. In this case the `label` field is `-1`.


### faiss\_search\_filter

```sql
CALL faiss_search_filter(string name, integer k, List q, string filter, string idselector, string tablename);
```

 - `name`: The name of the index that should be added to.
 - `k`: The amount of results to be returned.
 - `q`: The query vector for which the nearest neighbors should be computer.
 - `filter`: Expression to be used to select which rows are selected for search. If this expresion results in a 1 for a certain row, the result of `idselector` will be included in the search.
 - `idselector`: Expression to be used to select the id of the row.
 - `tablename`: The name of the table on which filter and idselector are executed.

And a variant with parameters:

```sql
CALL faiss_search_filter(string name, integer k, List q, MAP<string, string> parameters);
```

 - `parameters`: The parameters for searching the index. For example passing `{'efSearch': '1000'}` when searching`HNSW` index, will set the efSearch field to 1000. This is recursive, for example, when using an `IVF` with `HNSW`, `ivf.efSearch` can be used to set the `efSearch` value for the `HNSW` index. Note that currently, the implementation is verry limmited. Recursion is only implemented for `IDMap` and `IVF`, and only very few fields are implemented. This is because I did not need these for my thesis, but should be easy to add.

`faiss_search_filter` returns a list of structs with 3 fields: `rank` of type `INTEGER`, `label` of type `BIGINT`, `distance` of type `DISTANCE`. The length of this list is always `k`, even if not enough results were found. In this case the `label` field is `-1`.

For context, currently the way this filter is constructed, is by executing approximatly this query: `SELECT {filter}, {idselector} FROM {tablename}`. All the ids for which the filter is `1`, will be selected for search. The amount of rows in `table` is assumed to be equal to the amount of vectors. If this is not the case, the extension might crash. This function creates a bitmap of size `n` where `n` is the size of the table, this operation is `O(n)`. To improve performance, make sure that the ids are incremental, in order, and start at a multiple of 64. This greatly helps the performance of creating the bitmap.


### faiss\_search\_filter\_set

```sql
CALL faiss_search_filter\_set(string name, integer k, List q, string filter, string idselector, string tablename);
```

 - `name`: The name of the index that should be added to.
 - `k`: The amount of results to be returned.
 - `q`: The query vector for which the nearest neighbors should be computer.
 - `filter`: Expression to be used to select which rows are selected for search. If this expresion results in a 1 for a certain row, the result of `idselector` will be included in the search.
 - `idselector`: Expression to be used to select the id of the row.
 - `tablename`: The name of the table on which filter and idselector are executed.

And a variant with parameters:

```sql
CALL faiss_search_filter_set(string name, integer k, List q, MAP<string, string> parameters);
```

 - `parameters`: The parameters for searching the index. For example passing `{'efSearch': '1000'}` when searching`HNSW` index, will set the efSearch field to 1000. This is recursive, for example, when using an `IVF` with `HNSW`, `ivf.efSearch` can be used to set the `efSearch` value for the `HNSW` index. Note that currently, the implementation is verry limmited. Recursion is only implemented for `IDMap` and `IVF`, and only very few fields are implemented. This is because I did not need these for my thesis, but should be easy to add.

`faiss_search_filter_set` returns a list of structs with 3 fields: `rank` of type `INTEGER`, `label` of type `BIGINT`, `distance` of type `DISTANCE`. The length of this list is always `k`, even if not enough results were found. In this case the `label` field is `-1`.

For context, currently the way this filter is constructed, is by executing approximatly this query: `SELECT {idselector} FROM {tablename} WHERE {filter}=1`. All the ids for which the filter is `1`, will be selected for search. The amount of rows in `table` is assumed to be equal to the amount of vectors. If this is not the case, the extension might crash. This function creates a set of size `m` where `m` is the amount of vectors that are included in the search, this operation is `O(m)`.

 # TODO list

 Ideas that could still be implemented
  [ ] Use array of fixed size instead of lists as input for greater type-safety
  [ ] Check types of input, again for greater type-safety
  [ ] Add some kind of support for sequence vectors storage to duckdb, among other optimisations that would allow this extension to perform better
