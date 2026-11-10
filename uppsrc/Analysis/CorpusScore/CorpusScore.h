#ifndef _Analysis_CorpusScore_CorpusScore_h_
#define _Analysis_CorpusScore_CorpusScore_h_

#include <Core/Core.h>

NAMESPACE_UPP

struct CorpusMetric {
	double raw = 0;
	double z = 0;
};

struct CorpusScoreResult {
	Vector<String> tokens;
	CorpusMetric unigram;
	CorpusMetric bigram;
	CorpusMetric chargram;
	CorpusMetric cooccurrence;
	double       oov_rate = 0;
	double       composite = 0;
	int          length = 0;
};

class CorpusModel {
	struct LengthBucket {
		int lo = 0;
		int hi = -1;
		int sentence_count = 0;
	};

	struct Baseline {
		double mean = 0;
		double stddev = 1;
	};

	VectorMap<String, int64> unigram;
	VectorMap<String, int64> bigram;
	VectorMap<String, int64> chargram;
	VectorMap<String, int64> cooccurrence;
	VectorMap<String, String> meta;
	VectorMap<String, Baseline> baseline;
	Vector<LengthBucket> buckets;
	int64 total_tokens = 0;
	int64 total_sentences = 0;
	int64 total_chargrams = 0;

	String TablePath(const String& table_dir, const char* name) const;
	String PairKey(const String& a, const String& b) const;
	String BaselineKey(int bucket, const char* metric) const;
	int    FindBucket(int length) const;
	double GetNumber(const char* key, double default_value) const;
	double ScoreUnigram(const Vector<String>& tokens) const;
	double ScoreBigram(const Vector<String>& tokens) const;
	double ScoreChargram(const Vector<String>& tokens) const;
	double ScoreCooccurrence(const Vector<String>& tokens) const;
	double ZScore(int bucket, const char* metric, double raw) const;

public:
	static constexpr int FORMAT_VERSION = 1;

	static Vector<String> Tokenize(const String& text);
	static Vector<Vector<String>> SplitSentences(const String& text);

	bool BuildTables(const String& corpus_dir, const String& table_dir,
	                 const Vector<String>& excluded_genres, String& error);
	bool Load(const String& table_dir, String& error);
	CorpusScoreResult Score(const String& line) const;
};

END_UPP_NAMESPACE

#endif
