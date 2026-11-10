#include <Analysis/CorpusScore/CorpusScore.h>
#include <cmath>

using namespace Upp;

namespace {

String FindFixtureDir()
{
	Vector<String> candidates;
	candidates.Add(AppendFileName(GetCurrentDirectory(), "uppsrc/Analysis/CorpusScore/testdata"));
	candidates.Add(AppendFileName(GetFileFolder(GetExeFilePath()), "../uppsrc/Analysis/CorpusScore/testdata"));
	candidates.Add(AppendFileName(GetFileFolder(GetExeFilePath()), "../../uppsrc/Analysis/CorpusScore/testdata"));
	candidates.Add(AppendFileName(GetFileFolder(GetExeFilePath()), "../../../uppsrc/Analysis/CorpusScore/testdata"));

	for(const String& path : candidates) {
		if(DirectoryExists(AppendFileName(path, "corpus")))
			return NormalizePath(path);
	}
	return String();
}

void TestTokenization()
{
	Cout() << "TestTokenization: Starting..." << EOL;

	// Empty and punctuation strings
	ASSERT(CorpusModel::Tokenize("").IsEmpty());
	ASSERT(CorpusModel::Tokenize(" ... ,,, :; !? ").IsEmpty());

	// ASCII words with case normalization
	Vector<String> tokens = CorpusModel::Tokenize("Tama on pieni testimerkinta.");
	ASSERT(tokens.GetCount() == 4);
	ASSERT(tokens[0] == "tama");
	ASSERT(tokens[1] == "on");
	ASSERT(tokens[2] == "pieni");
	ASSERT(tokens[3] == "testimerkinta");

	// Finnish characters in UTF-8
	Vector<String> finnish_tokens = CorpusModel::Tokenize("Ääni kulkee yössä!");
	ASSERT(finnish_tokens.GetCount() == 3);
	ASSERT(finnish_tokens[0] == "ääni");
	ASSERT(finnish_tokens[1] == "kulkee");
	ASSERT(finnish_tokens[2] == "yössä");

	Cout() << "TestTokenization: Passed!" << EOL;
}

void TestSplitSentences()
{
	Cout() << "TestSplitSentences: Starting..." << EOL;

	String text = "Ensimmainen lause. Toinen lause! Kolmas lause? Neljas ilman pistetta";
	Vector<Vector<String>> sentences = CorpusModel::SplitSentences(text);
	ASSERT(sentences.GetCount() == 4);
	ASSERT(sentences[0].GetCount() == 2 && sentences[0][0] == "ensimmainen");
	ASSERT(sentences[1].GetCount() == 2 && sentences[1][0] == "toinen");
	ASSERT(sentences[2].GetCount() == 2 && sentences[2][0] == "kolmas");
	ASSERT(sentences[3].GetCount() == 3 && sentences[3][0] == "neljas");

	Cout() << "TestSplitSentences: Passed!" << EOL;
}

void TestBuildAndScore()
{
	Cout() << "TestBuildAndScore: Starting..." << EOL;

	String fixture_dir = FindFixtureDir();
	Cout() << "Fixture directory: " << fixture_dir << EOL;
	ASSERT_(!fixture_dir.IsEmpty(), "Test fixture directory not found");

	String corpus_dir = AppendFileName(fixture_dir, "corpus");
	String candidate_file = AppendFileName(fixture_dir, "candidates.txt");
	ASSERT(DirectoryExists(corpus_dir));
	ASSERT(FileExists(candidate_file));

	String temp_table_dir = AppendFileName(GetTempDirectory(), "corpus_score_check_tables_" + AsString(Random(1000000)));
	RealizeDirectory(temp_table_dir);

	String error;
	CorpusModel builder_model;
	Vector<String> excluded;
	excluded.Add("koodi");
	excluded.Add("english");

	bool built = builder_model.BuildTables(corpus_dir, temp_table_dir, excluded, error);
	if(!built) {
		Cerr() << "BuildTables error: " << error << EOL;
	}
	ASSERT(built);

	// Verify table files generated
	ASSERT(FileExists(AppendFileName(temp_table_dir, "meta.txt")));
	ASSERT(FileExists(AppendFileName(temp_table_dir, "sentence_length_buckets.tsv")));
	ASSERT(FileExists(AppendFileName(temp_table_dir, "unigram.tsv")));
	ASSERT(FileExists(AppendFileName(temp_table_dir, "bigram.tsv")));
	ASSERT(FileExists(AppendFileName(temp_table_dir, "chargram.tsv")));
	ASSERT(FileExists(AppendFileName(temp_table_dir, "cooccurrence.tsv")));
	ASSERT(FileExists(AppendFileName(temp_table_dir, "baseline.tsv")));

	// Test loading
	CorpusModel scorer_model;
	bool loaded = scorer_model.Load(temp_table_dir, error);
	if(!loaded) {
		Cerr() << "Load error: " << error << EOL;
	}
	ASSERT(loaded);

	// Test scoring known candidates
	FileIn in(candidate_file);
	ASSERT(!in.IsError());

	int line_count = 0;
	while(!in.IsEof()) {
		String line = in.GetLine();
		if(line.IsEmpty()) continue;
		line_count++;

		CorpusScoreResult res = scorer_model.Score(line);
		ASSERT(res.length > 0);
		ASSERT(res.tokens.GetCount() > 0);
		ASSERT(std::isfinite(res.composite));
		ASSERT(std::isfinite(res.unigram.raw));
		ASSERT(std::isfinite(res.bigram.raw));
		ASSERT(std::isfinite(res.chargram.raw));
		ASSERT(std::isfinite(res.cooccurrence.raw));
		ASSERT(res.oov_rate >= 0.0 && res.oov_rate <= 1.0);

		Cout() << "Scored line " << line_count << " (len=" << res.length
		       << ", oov=" << Format("%.2f", res.oov_rate)
		       << ", composite=" << Format("%.4f", res.composite)
		       << "): " << line << EOL;
	}
	ASSERT(line_count >= 3);

	// In-corpus sentence should have 0% OOV rate
	CorpusScoreResult known_res = scorer_model.Score("Tama on pieni testimerkinta.");
	ASSERT(known_res.oov_rate == 0.0);

	// Completely unrelated sentence should have high OOV rate (> 0.5)
	CorpusScoreResult foreign_res = scorer_model.Score("Completely unrelated English sentence with unknown vocabulary.");
	ASSERT(foreign_res.oov_rate > 0.5);

	// Cleanup temp files
	FileDelete(AppendFileName(temp_table_dir, "meta.txt"));
	FileDelete(AppendFileName(temp_table_dir, "sentence_length_buckets.tsv"));
	FileDelete(AppendFileName(temp_table_dir, "unigram.tsv"));
	FileDelete(AppendFileName(temp_table_dir, "bigram.tsv"));
	FileDelete(AppendFileName(temp_table_dir, "chargram.tsv"));
	FileDelete(AppendFileName(temp_table_dir, "cooccurrence.tsv"));
	FileDelete(AppendFileName(temp_table_dir, "baseline.tsv"));
	DirectoryDelete(temp_table_dir);

	Cout() << "TestBuildAndScore: Passed!" << EOL;
}

}

CONSOLE_APP_MAIN
{
	Cout() << "========================================" << EOL;
	Cout() << "Running CorpusScore Unit Tests" << EOL;
	Cout() << "========================================" << EOL;

	TestTokenization();
	TestSplitSentences();
	TestBuildAndScore();

	Cout() << "========================================" << EOL;
	Cout() << "ALL CORPUS SCORE TESTS PASSED!" << EOL;
	Cout() << "========================================" << EOL;
}
