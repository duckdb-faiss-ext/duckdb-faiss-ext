import duckdb

# need an unsigned flag for now
con = duckdb.connect(config = {'allow_unsigned_extensions': 'true'})
con.sql("LOAD '../build/release/extension/faiss/faiss.duckdb_extension'")

tweets_rel = con.from_query("FROM 'Tweets.csv.gz'").project('*, row_number() over () internal_tweet_id')

training_sample = tweets_rel.query("tweets", "FROM tweets USING SAMPLE 500 (reservoir, 42)")
testing_sample = tweets_rel.except_(training_sample).query("tweets_testing",  "FROM tweets USING SAMPLE 500 (reservoir, 42)")

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

def create_embeddings_rel(con, rel):
    return(con.from_df(create_embeddings(rel.project('internal_tweet_id, text').df())))

trainig_with_embeddings_rel = create_embeddings_rel(con, training_sample)

vector_length = trainig_with_embeddings_rel.aggregate('max(length(emb))').fetchone()[0]
con.sql(f"CALL FAISS_CREATE('flat', {vector_length}, 'IDMap,Flat')")
trainig_with_embeddings_rel.query('training', "CALL FAISS_ADD((SELECT internal_tweet_id, emb FROM training), 'flat')")

testing_with_embeddings_rel = create_embeddings_rel(con, testing_sample)

search_result_rel = testing_with_embeddings_rel.project("internal_tweet_id, UNNEST(FAISS_SEARCH('flat', 4, emb)).label other_tweet_id").set_alias('search_result')

print(testing_with_embeddings_rel.project("internal_tweet_id, FAISS_SEARCH('flat', 5, emb)"))

print(search_result_rel.aggregate('count()'))
search_with_other_sentiment_rel = search_result_rel.join(tweets_rel.set_alias('other_tweets'), 'other_tweet_id=other_tweets.internal_tweet_id').project('search_result.internal_tweet_id, other_tweet_id, airline_sentiment other_airline_sentiment')
print(search_with_other_sentiment_rel.aggregate('count()'))

# TODO use positional join here ^^ SELECT df1.*, df2.* FROM df1 POSITIONAL JOIN df2

# majority vote, which of the five other tweet's sentiments is most frequent?
search_majority_vote_rel = search_with_other_sentiment_rel.query("s", """
    WITH sentiments AS (SELECT internal_tweet_id, other_airline_sentiment, COUNT(*) sentiment_count FROM s GROUP BY internal_tweet_id, other_airline_sentiment)
    SELECT internal_tweet_id, FIRST(other_airline_sentiment) predicted_airline_sentiment FROM sentiments WHERE sentiment_count =
        (SELECT max(sentiment_count) FROM sentiments s2  WHERE sentiments.internal_tweet_id = s2.internal_tweet_id) GROUP BY internal_tweet_id
""")

# now lets see how good this was
ground_truth_comparision = search_majority_vote_rel.set_alias('search_result').join(tweets_rel.set_alias('other_tweets'), 'search_result.internal_tweet_id=other_tweets.internal_tweet_id').project('airline_sentiment = predicted_airline_sentiment correct_result').aggregate('count(), correct_result').order('correct_result')

print(ground_truth_comparision)
