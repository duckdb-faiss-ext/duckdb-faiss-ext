import duckdb
import time
import os

EXT_PATH = os.getenv('FAISS_EXTENSION_BINARY_PATH')

# need an unsigned flag for now
con = duckdb.connect(config = {'allow_unsigned_extensions': 'true'})
con.sql(f"LOAD '{EXT_PATH}'")
con.sql("CREATE TABLE input1 AS SELECT docid, vector AS emb FROM 'msmarco-passage-openai-ada2/0.jsonl.gz'")

vectors = con.from_query("SELECT * FROM input1")

vector_length = vectors.aggregate('max(len(emb))').fetchone()[0]
con.sql(f"CALL FAISS_CREATE('flat', {vector_length}, 'IVF65536_HNSW32,Flat')") # the index is based on https://github.com/facebookresearch/faiss/wiki/Guidelines-to-choose-an-index, quite important to use low memory and 10M-100M vectors. But PQ16 wasnt added because it requires training

print("loading in raw vectors")
con.sql("CALL FAISS_ADD((SELECT emb FROM input1), 'flat')")
print("saving index")
con.sql("CALL FAISS_SAVE('flat', 'index')")
print("querying original dataset")
result0 = con.from_query("SELECT UNNEST( FAISS_SEARCH('flat', 1, ["+"0,"*1535+"0])) AS result").df()
con.sql("CALL FAISS_DESTROY('flat')")
print("loading index")
con.sql("CALL FAISS_LOAD('flat', 'index')")
print("querying loaded dataset")
result1 = con.from_query("SELECT UNNEST( FAISS_SEARCH('flat', 1, ["+"0,"*1535+"0])) AS result").df()

if result0.equals(result1):
    print("queries result in the same answer!")
else:
    print("queries differ:")
    print(result0)
    print(result1)
    exit(1)

