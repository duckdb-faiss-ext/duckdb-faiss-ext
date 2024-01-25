import duckdb
import time
import os

clusters = 65536 // 32 # 65536 is the original, just divide to give a better solution
matching_pattern = "?"
vector_length = 1536

EXT_PATH = os.getenv('FAISS_EXTENSION_BINARY_PATH')

# need an unsigned flag for now
con = duckdb.connect(config = {'allow_unsigned_extensions': 'true'})
con.sql(f"LOAD '{EXT_PATH}'")
con.sql(f"CREATE TABLE input1 AS SELECT docid, vector AS emb FROM 'msmarco-passage-openai-ada2/{matching_pattern}.jsonl.gz'")

con.sql(f"CALL FAISS_CREATE('flat', {vector_length}, 'IVF{clusters}_HNSW32,Flat')") # the index is based on https://github.com/facebookresearch/faiss/wiki/Guidelines-to-choose-an-index, quite important to use low memory and 10M-100M vectors. But PQ16 wasnt added because it requires training

print("training index")
con.sql("CALL FAISS_MANUAL_TRAIN((SELECT emb FROM input1), 'flat')") # doing the training manually allows for less memory usage
print("adding vectors")
con.sql("CALL FAISS_ADD((SELECT docid, emb FROM input1), 'flat')")
print("saving index")
con.sql(f"CALL FAISS_SAVE('flat', 'index_2IVF{clusters}_{matching_pattern}')")
