import duckdb

import gzip
import pickle

# need an unsigned flag for now
con = duckdb.connect(config = {'allow_unsigned_extensions': 'true'})
con.sql("LOAD '../build/release/extension/faiss/faiss.duckdb_extension'")
con.sql("CREATE TABLE input1 AS SELECT docid, vector AS emb FROM '../../pyseriniSearch/anserini/collections/msmarco-passage-openai-ada2/0.jsonl.gz'")

vectors = con.from_query("SELECT * FROM input1")

vector_length = vectors.aggregate('max(len(emb))').fetchone()[0]
con.sql(f"CALL FAISS_CREATE('flat', {vector_length}, 'IDMap,Flat')")

con.sql("CALL FAISS_ADD((FROM input1), 'flat')")
con.sql("CALL FAISS_SAVE('flat', 'index')")
con.sql("CALL FAISS_LOAD('flat2', 'index')")
result0 = con.from_query("SELECT UNNEST( FAISS_SEARCH('flat', 1, ["+"0,"*1535+"0])) AS result").df()
result1 = con.from_query("SELECT UNNEST( FAISS_SEARCH('flat2', 1, ["+"0,"*1535+"0])) AS result").df()

if result0.equals(result1):
    print("SUCCESSFULL!")
else:
    print("FAIL")
    print(result0)
    print(result1)
    exit(1)

