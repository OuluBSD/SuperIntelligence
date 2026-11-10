#include "CorpusScore.h"

#include <cmath>

NAMESPACE_UPP

namespace {

struct RunningStats {
	int count = 0;
	double sum = 0;
	double sumsq = 0;

	void Add(double value) {
		count++;
		sum += value;
		sumsq += value * value;
	}
};

bool IsTimestampLine(const String& line)
{
	if(line.GetCount() != 5 || line[2] != ':')
		return false;
	return IsDigit(line[0]) && IsDigit(line[1]) && IsDigit(line[3]) && IsDigit(line[4]);
}

bool IsTokenByte(byte c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

Vector<String> Utf8Characters(const String& text)
{
	Vector<String> out;
	for(int i = 0; i < text.GetCount();) {
		byte c = (byte)text[i];
		int count = c >= 0xc0 ? (c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4) : 1;
		if(i + count > text.GetCount())
			break;
		out.Add(text.Mid(i, count));
		i += count;
	}
	return out;
}

void CollectCorpusFiles(const String& dir, Vector<String>& files)
{
	FindFile ff(AppendFileName(dir, "*"));
	while(ff) {
		String name = ff.GetName();
		if(name != "." && name != "..") {
			String path = AppendFileName(dir, name);
			if(ff.IsDirectory())
				CollectCorpusFiles(path, files);
			else if(ff.IsFile() && ToLower(GetFileExt(name)) == ".txt")
				files.Add(path);
		}
		ff.Next();
	}
}

bool IsExcludedGenre(const String& path, const Vector<String>& excluded_genres)
{
	String title = GetFileTitle(path);
	int split = title.ReverseFind('_');
	String genre = split < 0 ? "generic" : ToLower(title.Mid(split + 1));
	for(const String& excluded : excluded_genres)
		if(genre == ToLower(excluded))
			return true;
	return false;
}

bool WriteLine(FileOut& out, const String& line)
{
	out << line << '\n';
	return !out.IsError();
}

double NaturalLog(double value)
{
	ASSERT(std::isfinite(value));
	ASSERT(value > 0);
	return std::log(value);
}

}

String CorpusModel::TablePath(const String& table_dir, const char* name) const
{
	return AppendFileName(table_dir, name);
}

String CorpusModel::PairKey(const String& a, const String& b) const
{
	return a + "\1" + b;
}

String CorpusModel::BaselineKey(int bucket, const char* metric) const
{
	return AsString(bucket) + "\1" + metric;
}

int CorpusModel::FindBucket(int length) const
{
	ASSERT(length >= 0);
	ASSERT(!buckets.IsEmpty());
	for(int i = 0; i < buckets.GetCount(); i++)
		if(length >= buckets[i].lo && (buckets[i].hi < 0 || length <= buckets[i].hi))
			return i;
	ASSERT_(false, "Sentence length is outside every configured bucket");
	return 0;
}

double CorpusModel::GetNumber(const char* key, double default_value) const
{
	String value = meta.Get(key, "");
	double number = value.IsEmpty() ? default_value : StrDbl(value);
	ASSERT(std::isfinite(number));
	return number;
}

Vector<String> CorpusModel::Tokenize(const String& text)
{
	Vector<String> tokens;
	String token;
	for(int i = 0; i < text.GetCount();) {
		byte c = (byte)text[i];
		bool ascii = IsTokenByte(c);
		bool finnish = c == 0xc3 && i + 1 < text.GetCount()
			&& ((byte)text[i + 1] == 0xa4 || (byte)text[i + 1] == 0xb6 ||
			    (byte)text[i + 1] == 0xa5 || (byte)text[i + 1] == 0x84 ||
			    (byte)text[i + 1] == 0x96 || (byte)text[i + 1] == 0x85);
		if(ascii) {
			token.Cat((char)ToLower(c));
			i++;
		}
		else if(finnish) {
			byte second = (byte)text[i + 1];
			if(second == 0x84) second = 0xa4;
			if(second == 0x96) second = 0xb6;
			if(second == 0x85) second = 0xa5;
			token.Cat((char)0xc3);
			token.Cat((char)second);
			i += 2;
		}
		else {
			if(!token.IsEmpty()) {
				tokens.Add(token);
				token.Clear();
			}
			i++;
		}
	}
	if(!token.IsEmpty())
		tokens.Add(token);
	return tokens;
}

Vector<Vector<String>> CorpusModel::SplitSentences(const String& text)
{
	Vector<Vector<String>> sentences;
	String sentence;
	for(int i = 0; i < text.GetCount(); i++) {
		char c = text[i];
		sentence.Cat(c);
		bool last_dot = c == '.' && (i + 1 == text.GetCount() || text[i + 1] != '.');
		if(c == '!' || c == '?' || last_dot) {
			Vector<String> tokens = Tokenize(sentence);
			if(!tokens.IsEmpty())
				sentences.Add(pick(tokens));
			sentence.Clear();
		}
	}
	Vector<String> tokens = Tokenize(sentence);
	if(!tokens.IsEmpty())
		sentences.Add(pick(tokens));
	return sentences;
}

bool CorpusModel::BuildTables(const String& corpus_dir, const String& table_dir,
                              const Vector<String>& excluded_genres, String& error)
{
	if(!DirectoryExists(corpus_dir)) {
		error = "Corpus directory does not exist: " + corpus_dir;
		return false;
	}
	if(!RealizeDirectory(table_dir)) {
		error = "Cannot create table directory: " + table_dir;
		return false;
	}

	unigram.Clear();
	bigram.Clear();
	chargram.Clear();
	cooccurrence.Clear();
	meta.Clear();
	buckets.Clear();
	baseline.Clear();
	total_tokens = total_sentences = total_chargrams = 0;
	buckets.Add({ 1, 4, 0 });
	buckets.Add({ 5, 8, 0 });
	buckets.Add({ 9, 14, 0 });
	buckets.Add({ 15, -1, 0 });

	Vector<Vector<String>> sentences;
	Vector<String> files;
	CollectCorpusFiles(corpus_dir, files);
	for(const String& path : files) {
		if(IsExcludedGenre(path, excluded_genres))
			continue;
		String source = LoadFile(path);
		if(source.IsVoid()) {
			error = "Cannot read corpus file: " + path;
			return false;
		}
		String prose;
		for(const String& line : Split(source, '\n'))
			if(!IsTimestampLine(TrimBoth(line)))
				prose << line << '\n';
		Vector<Vector<String>> parsed = SplitSentences(prose);
		for(Vector<String>& words : parsed)
			sentences.Add(pick(words));
	}

	for(const Vector<String>& words : sentences) {
		ASSERT(!words.IsEmpty());
		total_sentences++;
		int bucket = FindBucket(words.GetCount());
		if(bucket >= 0)
			buckets[bucket].sentence_count++;
		for(int i = 0; i < words.GetCount(); i++) {
			ASSERT(!words[i].IsEmpty());
			unigram.GetAdd(words[i], 0)++;
			total_tokens++;
			if(i)
				bigram.GetAdd(PairKey(words[i - 1], words[i]), 0)++;
			Vector<String> chars = Utf8Characters("^" + words[i] + "$");
			for(int j = 0; j + 2 < chars.GetCount(); j++) {
				chargram.GetAdd(chars[j] + chars[j + 1] + chars[j + 2], 0)++;
				total_chargrams++;
			}
		}
	}

	const int min_cooc_count = 3;
	for(const Vector<String>& words : sentences) {
		Index<String> frequent;
		for(const String& word : words)
			if(unigram.Get(word, 0) >= min_cooc_count)
				frequent.FindAdd(word);
		for(int i = 0; i < frequent.GetCount(); i++)
			for(int j = i + 1; j < frequent.GetCount(); j++) {
				String a = frequent[i], b = frequent[j];
				if(b < a)
					Swap(a, b);
				cooccurrence.GetAdd(PairKey(a, b), 0)++;
			}
	}

	meta.GetAdd("format_version") = AsString(FORMAT_VERSION);
	meta.GetAdd("corpus_root") = corpus_dir;
	meta.GetAdd("excluded_genres") = Join(excluded_genres, ",");
	meta.GetAdd("total_tokens") = AsString(total_tokens);
	meta.GetAdd("total_sentences") = AsString(total_sentences);
	meta.GetAdd("vocab_size") = AsString(unigram.GetCount());
	meta.GetAdd("chargram_vocab_size") = AsString(chargram.GetCount());
	meta.GetAdd("total_chargrams") = AsString(total_chargrams);
	meta.GetAdd("min_cooc_count") = AsString(min_cooc_count);
	meta.GetAdd("unigram_alpha") = "0.1";
	meta.GetAdd("bigram_discount") = "0.4";
	meta.GetAdd("chargram_alpha") = "0.1";
	meta.GetAdd("oov_penalty") = "2.0";

	VectorMap<String, RunningStats> stats;
	for(const Vector<String>& words : sentences) {
		int bucket = FindBucket(words.GetCount());
		stats.GetAdd(BaselineKey(bucket, "unigram_logprob")).Add(ScoreUnigram(words));
		stats.GetAdd(BaselineKey(bucket, "bigram_logprob")).Add(ScoreBigram(words));
		stats.GetAdd(BaselineKey(bucket, "chargram_logprob")).Add(ScoreChargram(words));
		stats.GetAdd(BaselineKey(bucket, "cooc_pmi")).Add(ScoreCooccurrence(words));
	}
	for(int i = 0; i < stats.GetCount(); i++) {
		const RunningStats& value = stats[i];
		ASSERT(value.count > 0);
		ASSERT(std::isfinite(value.sum));
		ASSERT(std::isfinite(value.sumsq));
		Baseline& target = baseline.GetAdd(stats.GetKey(i));
		target.mean = value.count ? value.sum / value.count : 0;
		double variance = value.count ? value.sumsq / value.count - target.mean * target.mean : 0;
		target.stddev = variance > 1e-12 ? sqrt(variance) : 1;
		ASSERT(std::isfinite(target.mean));
		ASSERT(std::isfinite(target.stddev));
		ASSERT(target.stddev > 0);
	}

	FileOut meta_out(TablePath(table_dir, "meta.txt"));
	for(int i = 0; i < meta.GetCount(); i++)
		if(!WriteLine(meta_out, meta.GetKey(i) + "\t" + meta[i])) { error = "Cannot write meta.txt"; return false; }
	FileOut unigram_out(TablePath(table_dir, "unigram.tsv"));
	for(int i = 0; i < unigram.GetCount(); i++)
		if(!WriteLine(unigram_out, unigram.GetKey(i) + "\t" + AsString(unigram[i]))) { error = "Cannot write unigram.tsv"; return false; }
	FileOut bigram_out(TablePath(table_dir, "bigram.tsv"));
	for(int i = 0; i < bigram.GetCount(); i++) {
		Vector<String> parts = Split(bigram.GetKey(i), '\1');
		ASSERT(parts.GetCount() == 2);
		if(!WriteLine(bigram_out, parts[0] + "\t" + parts[1] + "\t" + AsString(bigram[i]))) { error = "Cannot write bigram.tsv"; return false; }
	}
	FileOut chargram_out(TablePath(table_dir, "chargram.tsv"));
	for(int i = 0; i < chargram.GetCount(); i++)
		if(!WriteLine(chargram_out, chargram.GetKey(i) + "\t" + AsString(chargram[i]))) { error = "Cannot write chargram.tsv"; return false; }
	FileOut cooc_out(TablePath(table_dir, "cooccurrence.tsv"));
	for(int i = 0; i < cooccurrence.GetCount(); i++) {
		Vector<String> parts = Split(cooccurrence.GetKey(i), '\1');
		ASSERT(parts.GetCount() == 2);
		if(!WriteLine(cooc_out, parts[0] + "\t" + parts[1] + "\t" + AsString(cooccurrence[i]))) { error = "Cannot write cooccurrence.tsv"; return false; }
	}
	FileOut bucket_out(TablePath(table_dir, "sentence_length_buckets.tsv"));
	for(const LengthBucket& bucket : buckets)
		if(!WriteLine(bucket_out, Format("%d\t%d\t%d", bucket.lo, bucket.hi, bucket.sentence_count))) { error = "Cannot write sentence_length_buckets.tsv"; return false; }
	FileOut baseline_out(TablePath(table_dir, "baseline.tsv"));
	for(int i = 0; i < baseline.GetCount(); i++) {
		Vector<String> parts = Split(baseline.GetKey(i), '\1');
		ASSERT(parts.GetCount() == 2);
		int bucket = StrInt(parts[0]);
		ASSERT(bucket >= 0 && bucket < buckets.GetCount());
		const LengthBucket& range = buckets[bucket];
		if(!WriteLine(baseline_out, Format("%d\t%d\t%s\t%.17g\t%.17g", range.lo, range.hi, parts[1], baseline[i].mean, baseline[i].stddev))) { error = "Cannot write baseline.tsv"; return false; }
	}
	return true;
}

double CorpusModel::ScoreUnigram(const Vector<String>& tokens) const
{
	if(tokens.IsEmpty()) return 0;
	double alpha = GetNumber("unigram_alpha", 0.1);
	double denominator = total_tokens + alpha * (unigram.GetCount() + 1);
	ASSERT(alpha > 0);
	ASSERT(denominator > 0 && std::isfinite(denominator));
	double sum = 0;
	for(const String& token : tokens)
		sum += NaturalLog((unigram.Get(token, 0) + alpha) / denominator);
	return sum / tokens.GetCount();
}

double CorpusModel::ScoreBigram(const Vector<String>& tokens) const
{
	if(tokens.GetCount() < 2) return ScoreUnigram(tokens);
	double discount = GetNumber("bigram_discount", 0.4);
	ASSERT(discount > 0 && discount <= 1);
	double unigram_score = ScoreUnigram(tokens);
	double sum = 0;
	for(int i = 1; i < tokens.GetCount(); i++) {
		int64 count = bigram.Get(PairKey(tokens[i - 1], tokens[i]), 0);
		int64 preceding = unigram.Get(tokens[i - 1], 0);
		sum += count && preceding ? NaturalLog((double)count / preceding) : NaturalLog(discount) + unigram_score;
	}
	return sum / (tokens.GetCount() - 1);
}

double CorpusModel::ScoreChargram(const Vector<String>& tokens) const
{
	if(tokens.IsEmpty()) return 0;
	double alpha = GetNumber("chargram_alpha", 0.1);
	double denominator = total_chargrams + alpha * (chargram.GetCount() + 1);
	ASSERT(alpha > 0);
	ASSERT(denominator > 0 && std::isfinite(denominator));
	double sum = 0;
	int count = 0;
	for(const String& token : tokens) {
		Vector<String> chars = Utf8Characters("^" + token + "$");
		for(int i = 0; i + 2 < chars.GetCount(); i++) {
			sum += NaturalLog((chargram.Get(chars[i] + chars[i + 1] + chars[i + 2], 0) + alpha) / denominator);
			count++;
		}
	}
	return count ? sum / count : 0;
}

double CorpusModel::ScoreCooccurrence(const Vector<String>& tokens) const
{
	if(tokens.GetCount() < 2 || !total_sentences) return 0;
	Index<String> words;
	for(const String& token : tokens)
		if(unigram.Get(token, 0) >= GetNumber("min_cooc_count", 3))
			words.FindAdd(token);
	double sum = 0;
	int count = 0;
	for(int i = 0; i < words.GetCount(); i++)
		for(int j = i + 1; j < words.GetCount(); j++) {
			String a = words[i], b = words[j];
			if(b < a) Swap(a, b);
			int64 pair = cooccurrence.Get(PairKey(a, b), 0);
			if(pair) {
				double denominator = (double)unigram.Get(a, 1) * unigram.Get(b, 1);
				ASSERT(pair > 0);
				ASSERT(denominator > 0 && std::isfinite(denominator));
				sum += NaturalLog((double)pair * total_sentences / denominator);
				count++;
			}
		}
	return count ? sum / count : 0;
}

double CorpusModel::ZScore(int bucket, const char* metric, double raw) const
{
	ASSERT(bucket >= 0 && bucket < buckets.GetCount());
	ASSERT(std::isfinite(raw));
	int index = baseline.Find(BaselineKey(bucket, metric));
	ASSERT_(index >= 0, "Missing baseline metric for sentence-length bucket");
	if(index < 0)
		return 0;
	Baseline value = baseline[index];
	ASSERT(std::isfinite(value.mean));
	ASSERT(std::isfinite(value.stddev));
	ASSERT(value.stddev > 0);
	return (raw - value.mean) / value.stddev;
}

CorpusScoreResult CorpusModel::Score(const String& line) const
{
	CorpusScoreResult result;
	result.tokens = Tokenize(line);
	result.length = result.tokens.GetCount();
	if(result.tokens.IsEmpty())
		return result;
	int bucket = FindBucket(result.length);
	result.unigram.raw = ScoreUnigram(result.tokens);
	result.bigram.raw = ScoreBigram(result.tokens);
	result.chargram.raw = ScoreChargram(result.tokens);
	result.cooccurrence.raw = ScoreCooccurrence(result.tokens);
	result.unigram.z = ZScore(bucket, "unigram_logprob", result.unigram.raw);
	result.bigram.z = ZScore(bucket, "bigram_logprob", result.bigram.raw);
	result.chargram.z = ZScore(bucket, "chargram_logprob", result.chargram.raw);
	result.cooccurrence.z = ZScore(bucket, "cooc_pmi", result.cooccurrence.raw);
	for(const String& token : result.tokens)
		if(unigram.Find(token) < 0)
			result.oov_rate += 1;
	result.oov_rate /= result.length;
	result.composite = (result.unigram.z + result.bigram.z + result.chargram.z + result.cooccurrence.z) / 4
		- GetNumber("oov_penalty", 2) * result.oov_rate;
	ASSERT(std::isfinite(result.unigram.raw));
	ASSERT(std::isfinite(result.bigram.raw));
	ASSERT(std::isfinite(result.chargram.raw));
	ASSERT(std::isfinite(result.cooccurrence.raw));
	ASSERT(std::isfinite(result.unigram.z));
	ASSERT(std::isfinite(result.bigram.z));
	ASSERT(std::isfinite(result.chargram.z));
	ASSERT(std::isfinite(result.cooccurrence.z));
	ASSERT(result.oov_rate >= 0 && result.oov_rate <= 1);
	ASSERT(std::isfinite(result.composite));
	return result;
}

bool CorpusModel::Load(const String& table_dir, String& error)
{
	meta.Clear(); unigram.Clear(); bigram.Clear(); chargram.Clear(); cooccurrence.Clear(); baseline.Clear(); buckets.Clear();
	FileIn in(TablePath(table_dir, "meta.txt"));
	if(in.IsError()) { error = "Cannot read meta.txt from: " + table_dir; return false; }
	while(!in.IsEof()) {
		Vector<String> row = Split(in.GetLine(), '\t');
		if(row.IsEmpty())
			continue;
		ASSERT_(row.GetCount() == 2, "Malformed meta.txt row");
		if(row.GetCount() != 2) { error = "Malformed meta.txt row"; return false; }
		meta.GetAdd(row[0]) = row[1];
	}
	int version = StrInt(meta.Get("format_version", "0"));
	if(version != FORMAT_VERSION) {
		error = Format("Unsupported corpus-score format version %d (expected %d)", version, FORMAT_VERSION);
		ASSERT_(false, error);
		return false;
	}
	auto load_count_table = [&](const char* name, int columns, VectorMap<String, int64>& target) {
		FileIn table(TablePath(table_dir, name));
		if(table.IsError()) { error = "Cannot read " + String(name); return false; }
		while(!table.IsEof()) {
			Vector<String> row = Split(table.GetLine(), '\t');
			if(row.IsEmpty()) continue;
			ASSERT_(row.GetCount() == columns, "Malformed count-table row");
			if(row.GetCount() != columns) { error = "Malformed row in " + String(name); return false; }
			String key = row[0];
			if(columns == 3) key = PairKey(row[0], row[1]);
			int64 value = StrInt64(row.Top());
			ASSERT(value > 0);
			if(value <= 0) { error = "Non-positive count in " + String(name); return false; }
			target.GetAdd(key) = value;
		}
		return true;
	};
	if(!load_count_table("unigram.tsv", 2, unigram) || !load_count_table("bigram.tsv", 3, bigram)
	   || !load_count_table("chargram.tsv", 2, chargram) || !load_count_table("cooccurrence.tsv", 3, cooccurrence))
		return false;
	FileIn bucket_in(TablePath(table_dir, "sentence_length_buckets.tsv"));
	while(!bucket_in.IsEof()) {
		Vector<String> row = Split(bucket_in.GetLine(), '\t');
		if(row.IsEmpty()) continue;
		ASSERT_(row.GetCount() == 3, "Malformed sentence-length bucket row");
		if(row.GetCount() != 3) { error = "Malformed sentence-length bucket row"; return false; }
		LengthBucket bucket = { StrInt(row[0]), StrInt(row[1]), StrInt(row[2]) };
		ASSERT(bucket.lo > 0 && (bucket.hi < 0 || bucket.lo <= bucket.hi));
		ASSERT(bucket.sentence_count >= 0);
		if(bucket.lo <= 0 || (bucket.hi >= 0 && bucket.lo > bucket.hi) || bucket.sentence_count < 0) {
			error = "Invalid sentence-length bucket";
			return false;
		}
		buckets.Add(bucket);
	}
	FileIn baseline_in(TablePath(table_dir, "baseline.tsv"));
	while(!baseline_in.IsEof()) {
		Vector<String> row = Split(baseline_in.GetLine(), '\t');
		if(row.IsEmpty()) continue;
		ASSERT_(row.GetCount() == 5, "Malformed baseline row");
		if(row.GetCount() != 5) { error = "Malformed baseline row"; return false; }
		int bucket = FindBucket(StrInt(row[0]));
		Baseline value = { StrDbl(row[3]), StrDbl(row[4]) };
		ASSERT(std::isfinite(value.mean));
		ASSERT(std::isfinite(value.stddev));
		ASSERT(value.stddev > 0);
		if(!std::isfinite(value.mean) || !std::isfinite(value.stddev) || value.stddev <= 0) {
			error = "Invalid baseline metric";
			return false;
		}
		baseline.GetAdd(BaselineKey(bucket, row[2])) = value;
	}
	total_tokens = StrInt64(meta.Get("total_tokens", "0"));
	total_sentences = StrInt64(meta.Get("total_sentences", "0"));
	total_chargrams = StrInt64(meta.Get("total_chargrams", "0"));
	if(buckets.IsEmpty() || !total_tokens || !total_sentences || !total_chargrams) {
		error = "Incomplete corpus-score table set: " + table_dir;
		return false;
	}
	ASSERT(unigram.GetCount() > 0);
	ASSERT(bigram.GetCount() > 0);
	ASSERT(chargram.GetCount() > 0);
	ASSERT(baseline.GetCount() > 0);
	return true;
}

END_UPP_NAMESPACE

#ifndef flagNO_MAIN

using namespace Upp;

CONSOLE_APP_MAIN
{
	const Vector<String>& args = CommandLine();
	String corpus_dir = AppendFileName(GetHomeDirFile("RemakingSteve"), "escsrc");
	String table_dir = AppendFileName(AppendFileName(GetHomeDirFile("RemakingSteve"), "analysis"), "corpus-score/1");
	String candidate_file;
	bool build_tables = false;
	for(int i = 0; i < args.GetCount(); i++) {
		if(args[i] == "--build-tables") build_tables = true;
		else if(args[i] == "--corpus" && i + 1 < args.GetCount()) corpus_dir = args[++i];
		else if(args[i] == "--table-dir" && i + 1 < args.GetCount()) table_dir = args[++i];
		else if(args[i] == "--file" && i + 1 < args.GetCount()) candidate_file = args[++i];
		else { Cout() << "Usage: CorpusScore --build-tables [--corpus DIR] [--table-dir DIR]\n"
		             "       CorpusScore --file CANDIDATES.txt [--table-dir DIR]\n"; SetExitCode(1); return; }
	}
	Vector<String> excluded;
	excluded.Add("koodi");
	excluded.Add("english");
	String error;
	CorpusModel model;
	if(build_tables) {
		if(!model.BuildTables(corpus_dir, table_dir, excluded, error)) { Cerr() << error << '\n'; SetExitCode(1); return; }
		Cout() << "Built corpus-score tables in " << table_dir << '\n';
		return;
	}
	if(candidate_file.IsEmpty()) { Cerr() << "--file is required unless --build-tables is specified\n"; SetExitCode(1); return; }
	if(!model.Load(table_dir, error)) { Cerr() << error << '\n'; SetExitCode(1); return; }
	FileIn candidates(candidate_file);
	if(candidates.IsError()) { Cerr() << "Cannot read candidate file: " << candidate_file << '\n'; SetExitCode(1); return; }
	while(!candidates.IsEof()) {
		String line = candidates.GetLine();
		if(line.IsEmpty()) continue;
		CorpusScoreResult result = model.Score(line);
		Cout() << Format("%.6f", result.composite) << '\t'
		       << Format("%.6f", result.unigram.raw) << '\t' << Format("%.6f", result.unigram.z) << '\t'
		       << Format("%.6f", result.bigram.raw) << '\t' << Format("%.6f", result.bigram.z) << '\t'
		       << Format("%.6f", result.chargram.raw) << '\t' << Format("%.6f", result.chargram.z) << '\t'
		       << Format("%.6f", result.cooccurrence.raw) << '\t' << Format("%.6f", result.cooccurrence.z) << '\t'
		       << Format("%.6f", result.oov_rate) << '\t' << result.length << '\t' << line << '\n';
	}
}

#endif
