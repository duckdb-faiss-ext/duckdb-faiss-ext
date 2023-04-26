import duckdb

tweets_rel = duckdb.from_query("FROM 'Tweets.csv.gz'")
tweets_subset_df = tweets_rel.project('tweet_id, text').limit(1000).df()


# ML Elvish
import torch
import transformers
import pandas

sbert_version = 'sentence-transformers/all-distilroberta-v1'
tokenizer = transformers.AutoTokenizer.from_pretrained(sbert_version)
model = transformers.AutoModel.from_pretrained(sbert_version)
encodings = tokenizer(tweets_subset_df['text'].tolist(), padding=True, truncation=True, max_length=200, return_tensors='pt')
model_output = model(**encodings)

def mean_pooling(model_output, attention_mask):
    token_embeddings = model_output[0]
    input_mask_expanded = attention_mask.unsqueeze(-1).expand(token_embeddings.size()).float()
    return torch.sum(token_embeddings * input_mask_expanded, 1) / torch.clamp(input_mask_expanded.sum(1), min=1e-9)

sentence_embeddings = torch.nn.functional.normalize(mean_pooling(model_output, encodings['attention_mask']), p=2, dim=1)

# look a squirrel
tweets_subset_df['emb'] = sentence_embeddings.tolist()
# End ML Elvish

# need an unsigned flag for now
con = duckdb.connect(config = {'allow_unsigned_extensions': 'true'})
tweets_subset_rel = duckdb.from_df(tweets_subset_df, connection = con)

vector_length = tweets_subset_rel.aggregate('max(length(emb))').fetchone()[0]

con.sql("LOAD '../build/release/extension/faiss/faiss.duckdb_extension'")
con.sql(f"CALL faiss_create('flat', {vector_length}, 'IDMap,Flat')")

tweets_subset_rel.create_view('emb')
con.sql("CALL faiss_add('flat', (SELECT tweet_id, emb FROM emb))")

print(con.sql("SELECT tweet_id, faiss_search('flat', 2, emb)  other_tweet_id FROM emb"))

