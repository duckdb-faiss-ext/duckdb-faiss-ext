package faissextcode

/*
#include <duckdb.h>
#include <stdio.h>
duckdb_database db;
duckdb_connection con;
void init() {
	duckdb_config config;

	// create the configuration object
	if (duckdb_create_config(&config) == DuckDBError) {
		printf("unable to create config\n");
	}
	// set some configuration options
	duckdb_set_config(config, "allow_unsigned_extensions", "true"); // or READ_ONLY

	// open the database using the configuration
	if (duckdb_open_ext(NULL, &db, config, NULL) == DuckDBError) {
		printf("unable to open database\n");
	}
	if (duckdb_connect(db, &con) == DuckDBError) {
		printf("unable to open connection\n");
	}

	duckdb_state state;

	state = duckdb_query(con, "CREATE TABLE ids AS SELECT (i)::BIGINT AS id, (i%100)::BIGINT AS sel FROM range(0, 8841823) tbl(i)", NULL);
	if (state == DuckDBError) {
		printf("unable to create mod table\n");
	}

	state = duckdb_query(con, "LOAD json;", NULL);
	if (state == DuckDBError) {
		printf("unable to load json extension\n");
	}

	state = duckdb_query(con, "LOAD 'build/reldebug/extension/faiss/faiss.duckdb_extension';", NULL);
	if (state == DuckDBError) {
		printf("unable to load faiss\n");
	}

	state = duckdb_query(con, "CALL FAISS_LOAD('flat', 'conformanceTests/index_IVF2048');", NULL);
	if (state == DuckDBError) {
		printf("unable to load faiss index\n");
	}

	state = duckdb_query(con, "CREATE TABLE queries AS SELECT qid, vector AS embedding FROM 'conformanceTests/anserini-tools/topics-and-qrels/topics.dl19-passage.openai-ada2.jsonl.gz'", NULL);
	if (state == DuckDBError) {
		printf("unable to create queries table\n");
	}
}

void run_post(uint64_t N, uint32_t n, uint32_t p) {
	duckdb_result result;
	duckdb_state state;
	char *query = (char*)malloc(1000 * sizeof(char));
	sprintf(query, "SELECT * FROM (SELECT qid, UNNEST(faiss_search('flat', %d, embedding), recursive:=true) FROM queries) JOIN ids ON label=id WHERE sel<%d", n, p);
	for (int i = 0; i < N; i++) {
		state = duckdb_query(con, query, &result);
		if (state == DuckDBError) {
			printf("unable to execute queries");
		}
		int resultn = duckdb_result_chunk_count(result);
		for (int i = 0; i < resultn; i++){
			duckdb_data_chunk chunk = duckdb_result_get_chunk(result, i);
			duckdb_destroy_data_chunk(&chunk);
		}
		duckdb_destroy_result(&result);
	}
	free(query);
}

void run_sel(uint64_t N, uint32_t p) {
	duckdb_result result;
	duckdb_state state;
	char *query = (char*)malloc(1000 * sizeof(char));
	sprintf(query, "SELECT * FROM (SELECT qid, UNNEST(faiss_search_filter('flat', 10, embedding, 'sel<%d', 'rowid', 'ids'), recursive:=true) FROM queries) JOIN ids ON label=id", p);
	for (int i = 0; i < N; i++) {
		state = duckdb_query(con, query, &result);
		if (state == DuckDBError) {
			printf("unable to execute queries");
		}
		int resultn = duckdb_result_chunk_count(result);
		for (int i = 0; i < resultn; i++){
			duckdb_data_chunk chunk = duckdb_result_get_chunk(result, i);
			duckdb_destroy_data_chunk(&chunk);
		}
		duckdb_destroy_result(&result);
	}
	free(query);
}

void run_set(uint64_t N, uint32_t p) {
	duckdb_result result;
	duckdb_state state;
	char *query = (char*)malloc(1000 * sizeof(char));
	sprintf(query, "SELECT * FROM (SELECT qid, UNNEST(faiss_search_filter_set('flat', 10, embedding, 'sel<%d', 'rowid', 'ids'), recursive:=true) FROM queries) JOIN ids ON label=id", p);
	for (int i = 0; i < N; i++) {
		state = duckdb_query(con, query, &result);
		if (state == DuckDBError) {
			printf("unable to execute queries");
		}
		int resultn = duckdb_result_chunk_count(result);
		for (int i = 0; i < resultn; i++){
			duckdb_data_chunk chunk = duckdb_result_get_chunk(result, i);
			duckdb_destroy_data_chunk(&chunk);
		}
		duckdb_destroy_result(&result);
	}
	free(query);
}

*/
import "C"

func benchinit() {
	C.init()
}

func benchrun_post(N uint64, n, p uint32) {
	C.run_post(C.uint64_t(N), C.uint32_t(n), C.uint32_t(p))
}

func benchrun_sel(N uint64, p uint32) {
	C.run_sel(C.uint64_t(N), C.uint32_t(p))
}

func benchrun_set(N uint64, p uint32) {
	C.run_set(C.uint64_t(N), C.uint32_t(p))
}
