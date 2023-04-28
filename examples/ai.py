import duckdb

# need an unsigned flag for now
con = duckdb.connect(config = {'allow_unsigned_extensions': 'true'})

tweets_rel = con.from_query("FROM 'Tweets.csv.gz'")

training_sample = tweets_rel.query("tweets", "FROM tweets USING SAMPLE 1000 (reservoir, 42)")
testing_sample = tweets_rel.except_(training_sample).query("testing", "FROM testing USING SAMPLE 1000 (reservoir, 43)")


# setup model
import torch
import transformers
import pandas

sbert_version = 'sentence-transformers/all-distilroberta-v1'
tokenizer = transformers.AutoTokenizer.from_pretrained(sbert_version)
model = transformers.AutoModel.from_pretrained(sbert_version)

def mean_pooling(model_output, attention_mask):
    token_embeddings = model_output[0]
    input_mask_expanded = attention_mask.unsqueeze(-1).expand(token_embeddings.size()).float()
    return torch.sum(token_embeddings * input_mask_expanded, 1) / torch.clamp(input_mask_expanded.sum(1), min=1e-9)

def create_embeddings(df):
    encodings = tokenizer(df['text'].tolist(), padding=True, truncation=True, max_length=160, return_tensors='pt')
    model_output = model(**encodings)
    sentence_embeddings = torch.nn.functional.normalize(mean_pooling(model_output, encodings['attention_mask']), p=2, dim=1)
    df['emb'] = sentence_embeddings.tolist()
    return(df)

def create_embeddings_rel(con, rel, viewname):
    df = create_embeddings(rel.project('tweet_id, text').df())
    rel = con.from_df(df)
    rel.create_view(viewname)
    return(rel)

trainig_with_embeddings_rel = create_embeddings_rel(con, training_sample, 'training')

vector_length = trainig_with_embeddings_rel.aggregate('max(length(emb))').fetchone()[0]
con.sql("LOAD '../build/release/extension/faiss/faiss.duckdb_extension'")
con.sql(f"CALL faiss_create('flat', {vector_length}, 'IDMap,Flat')")
con.sql("CALL faiss_add('flat', (SELECT tweet_id, emb FROM training))")

testing_with_embeddings_rel = create_embeddings_rel(con, testing_sample, 'testing')

search_result_rel = con.sql("SELECT tweet_id, unnest(faiss_search('flat', 5, emb)).label other_tweet_id FROM testing")
print(search_result_rel.aggregate('count()'))
search_with_other_sentiment_rel = search_result_rel.join(tweets_rel.set_alias('other_tweets'), 'other_tweet_id=other_tweets.tweet_id').project('query_relation.tweet_id, other_tweet_id, airline_sentiment other_airline_sentiment')
print(search_with_other_sentiment_rel.aggregate('count()'))

# majority vote, which of the five other tweet's sentiments is most frequent?
search_majority_vote_rel = search_with_other_sentiment_rel.query("s", """
    WITH sentiments AS (SELECT tweet_id, other_airline_sentiment, COUNT(*) sentiment_count FROM s GROUP BY tweet_id, other_airline_sentiment) 
    SELECT tweet_id, other_airline_sentiment predicted_airline_sentiment FROM sentiments WHERE sentiment_count = 
        (SELECT max(sentiment_count) FROM sentiments s2 WHERE sentiments.tweet_id = s2.tweet_id)
""")

# now lets see how good this was
ground_truth_comparision = search_majority_vote_rel.set_alias('search_result').join(tweets_rel.set_alias('other_tweets'), 'search_result.tweet_id=other_tweets.tweet_id').project('airline_sentiment = predicted_airline_sentiment correct_result').aggregate('count(), correct_result')

print(ground_truth_comparision)
