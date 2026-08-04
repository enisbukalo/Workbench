#include "modelsIni.h"
#include "configManager.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

/**
 * @brief Test fixture for ModelsIni.
 *
 * Sets HOME to a temp directory so ConfigManager/ModelsIni
 * operate on isolated files. Restores HOME on teardown.
 */
class ModelsIniTest : public ::testing::Test
{
  protected:
	void SetUp() override
	{
		// Save original HOME
		const char *h = std::getenv("HOME");
		m_originalHome = h ? h : "";

		// Create temp dir
		m_tmpDir = fs::temp_directory_path() / "workbench_test_ini";
		fs::create_directories(m_tmpDir);

		// Point HOME at our temp dir
		setenv("HOME", m_tmpDir.string().c_str(), 1);

		// Ensure .workbench subdir exists (ConfigManager expects it)
		m_configDir = m_tmpDir / ".workbench";
		fs::create_directories(m_configDir);
	}

	void TearDown() override
	{
		// Restore HOME
		if (m_originalHome.empty())
			unsetenv("HOME");
		else
			setenv("HOME", m_originalHome.c_str(), 1);

		// Clean up temp dir
		fs::remove_all(m_tmpDir);
	}

	/// Write a models.ini file with the given content
	void writeIni(const std::string &content)
	{
		std::ofstream f(m_configDir / "models.ini");
		f << content;
		f.close();
	}

	/// Read the current models.ini content
	std::string readIni()
	{
		std::ifstream f(m_configDir / "models.ini");
		return std::string((std::istreambuf_iterator<char>(f)),
						   std::istreambuf_iterator<char>());
	}

	/// Convenience: load the singleton after writing an ini
	bool loadIni(const std::string &content)
	{
		writeIni(content);
		return ModelsIni::instance().load();
	}

	fs::path m_tmpDir;
	fs::path m_configDir;
	std::string m_originalHome;
};

// =========================================================================
// INI Parsing
// =========================================================================

TEST_F(ModelsIniTest, LoadEmptyFile)
{
	EXPECT_TRUE(loadIni(""));
	EXPECT_TRUE(ModelsIni::instance().getModelNames().empty());
}

TEST_F(ModelsIniTest, LoadCommentOnlyFile)
{
	EXPECT_TRUE(loadIni("; This is a comment\n# Another comment\n"));
	EXPECT_TRUE(ModelsIni::instance().getModelNames().empty());
}

TEST_F(ModelsIniTest, LoadSingleSection)
{
	loadIni("[mymodel]\nmodel = /path/to/model.gguf\n");
	auto names = ModelsIni::instance().getModelNames();
	ASSERT_EQ(names.size(), 1u);
	EXPECT_EQ(names[0], "mymodel");
}

TEST_F(ModelsIniTest, LoadMultipleSections)
{
	loadIni("[model-a]\nmodel = /a.gguf\n\n[model-b]\nmodel = /b.gguf\n\n[model-c]\nmodel = /c.gguf\n");
	auto names = ModelsIni::instance().getModelNames();
	EXPECT_EQ(names.size(), 3u);
}

TEST_F(ModelsIniTest, LoadGlobalDefaults)
{
	loadIni("[*]\nn-gpu-layers = 99\nctx-size = 4096\n\n[mymodel]\nmodel = /m.gguf\n");
	auto defaults = ModelsIni::instance().getGlobalDefaults();
	EXPECT_EQ(defaults["n-gpu-layers"], "99");
	EXPECT_EQ(defaults["ctx-size"], "4096");
	// Global section should not appear in model names
	auto names = ModelsIni::instance().getModelNames();
	EXPECT_EQ(names.size(), 1u);
}

TEST_F(ModelsIniTest, LoadMissingFile)
{
	// Don't write any file
	EXPECT_FALSE(ModelsIni::instance().load());
}

TEST_F(ModelsIniTest, LoadKeyValueWithSpaces)
{
	loadIni("[model]\n  model  =  /spaced/path.gguf  \n  ctx-size  =  8192  \n");
	EXPECT_EQ(ModelsIni::instance().getModelPath("model"), "/spaced/path.gguf");
	auto vals = ModelsIni::instance().getSectionValues("model");
	EXPECT_EQ(vals["ctx-size"], "8192");
}

TEST_F(ModelsIniTest, LoadSectionBracketEdgeCases)
{
	// Section with spaces inside brackets
	loadIni("[ spaced name ]\nmodel = /m.gguf\n");
	auto names = ModelsIni::instance().getModelNames();
	ASSERT_EQ(names.size(), 1u);
	EXPECT_EQ(names[0], "spaced name");
}

// =========================================================================
// Getter Methods
// =========================================================================

TEST_F(ModelsIniTest, GetModelNames_SkipsStar)
{
	loadIni("[*]\nc = 4096\n\n[alpha]\nmodel = /a.gguf\n\n[beta]\nmodel = /b.gguf\n");
	auto names = ModelsIni::instance().getModelNames();
	EXPECT_EQ(names.size(), 2u);
	for (const auto &n : names)
		EXPECT_NE(n, "*");
}

TEST_F(ModelsIniTest, GetModelPath_Found)
{
	loadIni("[test]\nmodel = /test/model.gguf\n");
	EXPECT_EQ(ModelsIni::instance().getModelPath("test"), "/test/model.gguf");
}

TEST_F(ModelsIniTest, GetModelPath_NotFound)
{
	loadIni("[test]\nmodel = /m.gguf\n");
	EXPECT_EQ(ModelsIni::instance().getModelPath("nonexistent"), "");
}

TEST_F(ModelsIniTest, GetSectionValues_Found)
{
	loadIni("[test]\nmodel = /m.gguf\nctx-size = 2048\nbatch-size = 512\n");
	auto vals = ModelsIni::instance().getSectionValues("test");
	EXPECT_EQ(vals.size(), 3u);
	EXPECT_EQ(vals["model"], "/m.gguf");
	EXPECT_EQ(vals["ctx-size"], "2048");
}

TEST_F(ModelsIniTest, GetSectionValues_NotFound)
{
	loadIni("[test]\nmodel = /m.gguf\n");
	auto vals = ModelsIni::instance().getSectionValues("missing");
	EXPECT_TRUE(vals.empty());
}

TEST_F(ModelsIniTest, GetGlobalDefaults_Present)
{
	loadIni("[*]\nn-gpu-layers = all\n");
	auto defaults = ModelsIni::instance().getGlobalDefaults();
	EXPECT_EQ(defaults["n-gpu-layers"], "all");
}

TEST_F(ModelsIniTest, GetGlobalDefaults_Absent)
{
	loadIni("[test]\nmodel = /m.gguf\n");
	auto defaults = ModelsIni::instance().getGlobalDefaults();
	EXPECT_TRUE(defaults.empty());
}

// =========================================================================
// Preset Deserialization
// =========================================================================

TEST_F(ModelsIniTest, GetPreset_ValidSection)
{
	loadIni("[mypreset]\nmodel = /m.gguf\nn-gpu-layers = 33\nctx-size = 4096\ntemp = 0.7\ntop-p = 0.9\n");
	auto preset = ModelsIni::instance().getPreset("mypreset");
	ASSERT_TRUE(preset.has_value());
	EXPECT_EQ(preset->name, "mypreset");
	EXPECT_EQ(preset->model, "/m.gguf");
	EXPECT_EQ(preset->load.ngpuLayers, 33);
	EXPECT_EQ(preset->load.ctxSize, 4096);
	EXPECT_DOUBLE_EQ(preset->inference.temperature, 0.7);
	EXPECT_DOUBLE_EQ(preset->inference.topP, 0.9);
}

TEST_F(ModelsIniTest, GetPreset_StarReturnsNullopt)
{
	loadIni("[*]\nc = 4096\n");
	EXPECT_FALSE(ModelsIni::instance().getPreset("*").has_value());
}

TEST_F(ModelsIniTest, GetPreset_UnknownReturnsNullopt)
{
	loadIni("[test]\nmodel = /m.gguf\n");
	EXPECT_FALSE(ModelsIni::instance().getPreset("unknown").has_value());
}

TEST_F(ModelsIniTest, GetPreset_InfPrefixKeys)
{
	loadIni("[model]\nmodel = /m.gguf\ninf.temp = 1.2\ninf.top-k = 50\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	EXPECT_DOUBLE_EQ(preset->inference.temperature, 1.2);
	EXPECT_EQ(preset->inference.topK, 50);
}

TEST_F(ModelsIniTest, GetPreset_KebabCaseKeys)
{
	loadIni("[model]\nmodel = /m.gguf\nn-gpu-layers = 20\nctx-size = 2048\nbatch-size = 256\nflash-attn = off\nrepeat-penalty = 1.1\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	EXPECT_EQ(preset->load.ngpuLayers, 20);
	EXPECT_EQ(preset->load.ctxSize, 2048);
	EXPECT_EQ(preset->load.batchSize, 256);
	EXPECT_EQ(preset->load.flashAttn, "off");
	EXPECT_DOUBLE_EQ(preset->inference.repeatPenalty, 1.1);
}

TEST_F(ModelsIniTest, GetPreset_UnderscoreKeys)
{
	loadIni("[model]\nmodel = /m.gguf\nflash_attn = on\ntop_p = 0.85\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	EXPECT_EQ(preset->load.flashAttn, "on");
	EXPECT_DOUBLE_EQ(preset->inference.topP, 0.85);
}

TEST_F(ModelsIniTest, GetPreset_LoadSettingsAllFields)
{
	loadIni(
		"[model]\nmodel = /m.gguf\n"
		"n-gpu-layers = 40\nctx-size = 8192\nbatch-size = 1024\n"
		"ubatch-size = 256\nparallel = 2\nflash-attn = on\n"
		"kv-offload = true\nkv-unified = false\nmmap = true\nmlock = true\n"
		"fit = false\nsplit-mode = layer\ntensor-split = 1,1\n"
		"cache-type-k = q8_0\ncache-type-v = q4_0\n"
		"lora = /lora.bin\nmmproj = /proj.bin\n"
		"model-draft = /draft.gguf\nspec-draft-n-max = 16\n"
		"cache-type-k-draft = q8_0\ncache-type-v-draft = q8_0\n"
		"device-draft = 0,1,2\nchat-template-kwargs = {\"preserve_thinking\": true}\n"
		"chat-template = chatml\nreasoning-format = hidden\nspec-type = draft-mtp\n"
		"threads = 8\nthreads-batch = 4\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	auto &l = preset->load;
	EXPECT_EQ(l.ngpuLayers, 40);
	EXPECT_EQ(l.ctxSize, 8192);
	EXPECT_EQ(l.batchSize, 1024);
	EXPECT_EQ(l.ubatchSize, 256);
	EXPECT_EQ(l.parallel, 2);
	EXPECT_EQ(l.flashAttn, "on");
	EXPECT_TRUE(l.kvOffload);
	EXPECT_FALSE(l.kvUnified);
	EXPECT_TRUE(l.mmap);
	EXPECT_TRUE(l.mlock);
	EXPECT_FALSE(l.fit);
	EXPECT_EQ(l.splitMode, "layer");
	EXPECT_EQ(l.tensorSplit, "1,1");
	EXPECT_EQ(l.cacheTypeK, "q8_0");
	EXPECT_EQ(l.cacheTypeV, "q4_0");
	EXPECT_EQ(l.lora, "/lora.bin");
	EXPECT_EQ(l.mmproj, "/proj.bin");
	EXPECT_EQ(l.modelDraft, "/draft.gguf");
	EXPECT_EQ(l.draftMax, 16);
	EXPECT_EQ(l.cacheTypeKDraft, "q8_0");
	EXPECT_EQ(l.cacheTypeVDraft, "q8_0");
	EXPECT_EQ(l.deviceDraft, "0,1,2");
	EXPECT_TRUE(l.preserveThinking);
	EXPECT_EQ(l.chatTemplate, "chatml");
	EXPECT_EQ(l.reasoningFormat, "hidden");
	EXPECT_EQ(l.specType, "draft-mtp");
	EXPECT_EQ(l.threads, 8);
	EXPECT_EQ(l.threadsBatch, 4);
}

TEST_F(ModelsIniTest, GetPreset_InferenceSettingsAllFields)
{
	loadIni(
		"[model]\nmodel = /m.gguf\n"
		"temp = 1.5\ntop-p = 0.8\ntop-k = 100\nmin-p = 0.1\n"
		"repeat-penalty = 1.3\npresence-penalty = 0.5\nfrequency-penalty = 0.3\n"
		"n-predict = 512\nseed = 42\nsamplers = topK;topP;temperature\n"
		"repeat-last-n = 128\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	auto &i = preset->inference;
	EXPECT_DOUBLE_EQ(i.temperature, 1.5);
	EXPECT_DOUBLE_EQ(i.topP, 0.8);
	EXPECT_EQ(i.topK, 100);
	EXPECT_DOUBLE_EQ(i.minP, 0.1);
	EXPECT_DOUBLE_EQ(i.repeatPenalty, 1.3);
	EXPECT_DOUBLE_EQ(i.presencePenalty, 0.5);
	EXPECT_DOUBLE_EQ(i.frequencyPenalty, 0.3);
	EXPECT_EQ(i.nPredict, 512);
	EXPECT_EQ(i.seed, 42);
	EXPECT_EQ(i.samplers, "topK;topP;temperature");
	EXPECT_EQ(i.repeatLastN, 128);
}

TEST_F(ModelsIniTest, GetPreset_Phase4InferenceFields)
{
	loadIni(
		"[model]\nmodel = /m.gguf\n"
		"ignore-eos = true\n"
		"logit-bias = 1234,+0.5\n"
		"adaptive-target = 3.75\n"
		"adaptive-decay = 0.8\n"
		"grammar-file = /path/grammar.bnf\n"
		"json-schema-file = /path/schema.json\n"
		"sampler-seq = tKpT\n"
		"dry-sequence-breaker = \\n\\n\n"
		"backend-sampling = true\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	auto &i = preset->inference;
	EXPECT_EQ(i.ignoreEos, true);
	EXPECT_EQ(i.logitBias, "1234,+0.5");
	EXPECT_DOUBLE_EQ(i.adaptiveTarget, 3.75);
	EXPECT_DOUBLE_EQ(i.adaptiveDecay, 0.8);
	EXPECT_EQ(i.grammarFile, "/path/grammar.bnf");
	EXPECT_EQ(i.jsonSchemaFile, "/path/schema.json");
	EXPECT_EQ(i.samplerSeq, "tKpT");
	EXPECT_EQ(i.drySequenceBreaker, "\\n\\n");
	EXPECT_EQ(i.backendSampling, true);
}

TEST_F(ModelsIniTest, GetPreset_DefaultsWhenEmpty)
{
	loadIni("[model]\nmodel = /m.gguf\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	// Check load defaults
	Config::LoadSettings defaultLoad;
	EXPECT_EQ(preset->load.ctxSize, defaultLoad.ctxSize);
	EXPECT_EQ(preset->load.batchSize, defaultLoad.batchSize);
	// Check inference defaults
	Config::InferenceSettings defaultInf;
	EXPECT_DOUBLE_EQ(preset->inference.temperature, defaultInf.temperature);
	EXPECT_EQ(preset->inference.topK, defaultInf.topK);
}

TEST_F(ModelsIniTest, SavePreset_Phase4InferenceRoundTrip)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "phase4test";
	preset.model = "/rt.gguf";
	preset.inference.ignoreEos = true;
	preset.inference.logitBias = "-123,+0.5,456,-0.3";
	preset.inference.adaptiveTarget = 5.0;
	preset.inference.adaptiveDecay = 0.25;
	preset.inference.jsonSchemaFile = "/schema.json";
	preset.inference.grammarFile = "/grammar.bnf";
	preset.inference.samplerSeq = "KpTt";
	preset.inference.drySequenceBreaker = "---";
	preset.inference.backendSampling = true;

	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("phase4test");
	ASSERT_TRUE(loaded.has_value());
 EXPECT_EQ(loaded->inference.ignoreEos, true);
	EXPECT_EQ(loaded->inference.logitBias, "-123,+0.5,456,-0.3");
	EXPECT_DOUBLE_EQ(loaded->inference.adaptiveTarget, 5.0);
	EXPECT_DOUBLE_EQ(loaded->inference.adaptiveDecay, 0.25);
	EXPECT_EQ(loaded->inference.jsonSchemaFile, "/schema.json");
	EXPECT_EQ(loaded->inference.grammarFile, "/grammar.bnf");
	EXPECT_EQ(loaded->inference.samplerSeq, "KpTt");
	EXPECT_EQ(loaded->inference.drySequenceBreaker, "---");
	EXPECT_EQ(loaded->inference.backendSampling, true);
}

TEST_F(ModelsIniTest, SavePreset_Phase3LoadRoundTrip)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "phase3test";
	preset.model = "/rt.gguf";
	preset.load.nCpuMoe = 16;
	preset.load.cpuMoe = true;
	preset.load.overrideTensor = "attn.*=CPU";
	preset.load.ropeScaling = "yarn";
	preset.load.ropeScale = 2.0;
	preset.load.ropeFreqBase = 10000.0;
	preset.load.ropeFreqScale = 0.5;
	preset.load.yarnOrigCtx = 4096;
	preset.load.yarnExtFactor = 0.5;
	preset.load.yarnAttnFactor = 2.0;
	preset.load.yarnBetaSlow = 2.0;
	preset.load.yarnBetaFast = 64.0;
	preset.load.swaFull = true;
	preset.load.keep = 256;
	preset.load.numa = "isolate";
	preset.load.fitTarget = "vram";
	preset.load.fitCtx = 8192;
	preset.load.checkTensors = true;
	preset.load.overrideKv = "tokenizer.ggml.bos_token_id=int:1";
	preset.load.loraScaled = "adapter.gguf=0.8";
	preset.load.controlVector = "/cv.gguf";
	preset.load.controlVectorScaled = "/cv.gguf=0.5";
	preset.load.specDraftNMin = 4;
	preset.load.specDraftPMin = 0.6;
	preset.load.specDraftPSplit = 0.1;
	preset.load.cpuMoeDraft = true;

	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("phase3test");
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->load.nCpuMoe, 16);
	EXPECT_EQ(loaded->load.cpuMoe, true);
	EXPECT_EQ(loaded->load.overrideTensor, "attn.*=CPU");
	EXPECT_EQ(loaded->load.ropeScaling, "yarn");
	EXPECT_DOUBLE_EQ(loaded->load.ropeScale, 2.0);
	EXPECT_DOUBLE_EQ(loaded->load.ropeFreqBase, 10000.0);
	EXPECT_DOUBLE_EQ(loaded->load.ropeFreqScale, 0.5);
	EXPECT_EQ(loaded->load.yarnOrigCtx, 4096);
	EXPECT_DOUBLE_EQ(loaded->load.yarnExtFactor, 0.5);
	EXPECT_DOUBLE_EQ(loaded->load.yarnAttnFactor, 2.0);
	EXPECT_DOUBLE_EQ(loaded->load.yarnBetaSlow, 2.0);
	EXPECT_DOUBLE_EQ(loaded->load.yarnBetaFast, 64.0);
	EXPECT_EQ(loaded->load.swaFull, true);
	EXPECT_EQ(loaded->load.keep, 256);
	EXPECT_EQ(loaded->load.numa, "isolate");
	EXPECT_EQ(loaded->load.fitTarget, "vram");
	EXPECT_EQ(loaded->load.fitCtx, 8192);
	EXPECT_EQ(loaded->load.checkTensors, true);
	EXPECT_EQ(loaded->load.overrideKv, "tokenizer.ggml.bos_token_id=int:1");
	EXPECT_EQ(loaded->load.loraScaled, "adapter.gguf=0.8");
	EXPECT_EQ(loaded->load.controlVector, "/cv.gguf");
	EXPECT_EQ(loaded->load.controlVectorScaled, "/cv.gguf=0.5");
	EXPECT_EQ(loaded->load.specDraftNMin, 4);
	EXPECT_DOUBLE_EQ(loaded->load.specDraftPMin, 0.6);
	EXPECT_DOUBLE_EQ(loaded->load.specDraftPSplit, 0.1);
	EXPECT_EQ(loaded->load.cpuMoeDraft, true);
}

// =========================================================================
// Preset Write Methods
// =========================================================================

TEST_F(ModelsIniTest, SavePreset_NewSection)
{
	loadIni("[existing]\nmodel = /a.gguf\n");
	Config::ModelPreset preset;
	preset.name = "newpreset";
	preset.model = "/new.gguf";
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	// Verify it was saved
	auto names = ModelsIni::instance().getModelNames();
	EXPECT_EQ(names.size(), 2u);
	EXPECT_EQ(ModelsIni::instance().getModelPath("newpreset"), "/new.gguf");
}

TEST_F(ModelsIniTest, SavePreset_OverwriteExisting)
{
	loadIni("[mymodel]\nmodel = /old.gguf\nctx-size = 1024\n");
	Config::ModelPreset preset;
	preset.name = "mymodel";
	preset.model = "/new.gguf";
	preset.load.ctxSize = 8192;
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	EXPECT_EQ(ModelsIni::instance().getModelPath("mymodel"), "/new.gguf");
	auto reloaded = ModelsIni::instance().getPreset("mymodel");
	ASSERT_TRUE(reloaded.has_value());
	EXPECT_EQ(reloaded->load.ctxSize, 8192);
}

TEST_F(ModelsIniTest, SavePreset_EmptyNameFails)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "";
	preset.model = "/m.gguf";
	EXPECT_FALSE(ModelsIni::instance().savePreset(preset));
}

TEST_F(ModelsIniTest, SavePreset_RoundTrip)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "roundtrip";
	preset.model = "/rt.gguf";
	preset.load.ctxSize = 4096;
	preset.load.batchSize = 512;
	preset.load.ngpuLayers = 33;
	preset.inference.temperature = 0.7;
	preset.inference.topP = 0.9;
	preset.inference.topK = 50;
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("roundtrip");
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->model, "/rt.gguf");
	EXPECT_EQ(loaded->load.ctxSize, 4096);
	EXPECT_EQ(loaded->load.batchSize, 512);
	EXPECT_EQ(loaded->load.ngpuLayers, 33);
	EXPECT_DOUBLE_EQ(loaded->inference.temperature, 0.7);
	EXPECT_DOUBLE_EQ(loaded->inference.topP, 0.9);
	EXPECT_EQ(loaded->inference.topK, 50);
}

TEST_F(ModelsIniTest, RenamePreset_Success)
{
	loadIni("[old-name]\nmodel = /m.gguf\nctx-size = 2048\n");
	EXPECT_TRUE(ModelsIni::instance().renamePreset("old-name", "new-name"));

	EXPECT_FALSE(ModelsIni::instance().getPreset("old-name").has_value());
	auto renamed = ModelsIni::instance().getPreset("new-name");
	ASSERT_TRUE(renamed.has_value());
	EXPECT_EQ(renamed->model, "/m.gguf");
	EXPECT_EQ(renamed->load.ctxSize, 2048);
}

TEST_F(ModelsIniTest, RenamePreset_DuplicateNameFails)
{
	loadIni("[alpha]\nmodel = /a.gguf\n\n[beta]\nmodel = /b.gguf\n");
	EXPECT_FALSE(ModelsIni::instance().renamePreset("alpha", "beta"));
}

TEST_F(ModelsIniTest, RenamePreset_StarNameFails)
{
	loadIni("[test]\nmodel = /m.gguf\n");
	EXPECT_FALSE(ModelsIni::instance().renamePreset("test", "*"));
}

TEST_F(ModelsIniTest, RenamePreset_NotFoundFails)
{
	loadIni("[test]\nmodel = /m.gguf\n");
	EXPECT_FALSE(ModelsIni::instance().renamePreset("nonexistent", "newname"));
}

TEST_F(ModelsIniTest, DeletePreset_Success)
{
	loadIni("[alpha]\nmodel = /a.gguf\n\n[beta]\nmodel = /b.gguf\n");
	EXPECT_TRUE(ModelsIni::instance().deletePreset("alpha"));

	auto names = ModelsIni::instance().getModelNames();
	EXPECT_EQ(names.size(), 1u);
	EXPECT_EQ(names[0], "beta");
}

TEST_F(ModelsIniTest, DeletePreset_NotFound)
{
	loadIni("[test]\nmodel = /m.gguf\n");
	EXPECT_FALSE(ModelsIni::instance().deletePreset("nonexistent"));
}

// =========================================================================
// Collection Queries
// =========================================================================

TEST_F(ModelsIniTest, GetPresetsForModel_MultiplePresets)
{
	loadIni("[preset-a]\nmodel = /shared.gguf\nctx-size = 2048\n\n"
			"[preset-b]\nmodel = /shared.gguf\nctx-size = 4096\n\n"
			"[other]\nmodel = /different.gguf\n");
	auto presets =
		ModelsIni::instance().getPresetsForModel("/shared.gguf");
	EXPECT_EQ(presets.size(), 2u);
}

TEST_F(ModelsIniTest, GetPresetsForModel_NoMatch)
{
	loadIni("[test]\nmodel = /m.gguf\n");
	auto presets =
		ModelsIni::instance().getPresetsForModel("/nonexistent.gguf");
	EXPECT_TRUE(presets.empty());
}

TEST_F(ModelsIniTest, GetUniqueModelEntries_Deduplicates)
{
	loadIni("[preset-a]\nmodel = /shared.gguf\n\n"
			"[preset-b]\nmodel = /shared.gguf\n\n"
			"[other]\nmodel = /different.gguf\n");
	auto entries = ModelsIni::instance().getUniqueModelEntries();
	EXPECT_EQ(entries.size(), 2u);
}

TEST_F(ModelsIniTest, GetUniqueModelEntries_SkipsEmptyModel)
{
	loadIni("[no-model]\nctx-size = 2048\n\n[has-model]\nmodel = /m.gguf\n");
	auto entries = ModelsIni::instance().getUniqueModelEntries();
	EXPECT_EQ(entries.size(), 1u);
	EXPECT_EQ(entries[0].modelPath, "/m.gguf");
}

// =========================================================================
// Edge Cases
// =========================================================================

TEST_F(ModelsIniTest, SanitiseSectionName_RemovesBrackets)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "[bad]name";
	preset.model = "/m.gguf";
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));
	// Should be saved without brackets
	EXPECT_TRUE(ModelsIni::instance().getPreset("badname").has_value());
}

TEST_F(ModelsIniTest, CreateDefault_WritesAndLoads)
{
	// No file exists yet
	EXPECT_TRUE(ModelsIni::instance().createDefault());
	// Should have created the file and loaded it
	EXPECT_FALSE(ModelsIni::instance().getPath().empty());
}

TEST_F(ModelsIniTest, BoolParsing_TrueVariants)
{
	loadIni("[model]\nmodel = /m.gguf\nmmap = true\nmlock = on\nkv-offload = 1\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	EXPECT_TRUE(preset->load.mmap);
	EXPECT_TRUE(preset->load.mlock);
	EXPECT_TRUE(preset->load.kvOffload);
}

TEST_F(ModelsIniTest, BoolParsing_FalseVariants)
{
	loadIni("[model]\nmodel = /m.gguf\nmmap = false\nmlock = off\nkv-offload = 0\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	EXPECT_FALSE(preset->load.mmap);
	EXPECT_FALSE(preset->load.mlock);
	EXPECT_FALSE(preset->load.kvOffload);
}

TEST_F(ModelsIniTest, InvalidIntFallsBackToDefault)
{
	loadIni("[model]\nmodel = /m.gguf\nctx-size = notanumber\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	Config::LoadSettings defaultLoad;
	EXPECT_EQ(preset->load.ctxSize, defaultLoad.ctxSize);
}

// =============================================================================
// spec-type read/write tests
// =============================================================================

TEST_F(ModelsIniTest, GetPreset_SpecTypeRead)
{
	loadIni("[model]\nmodel = /m.gguf\nspec-type = draft-mtp\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	EXPECT_EQ(preset->load.specType, "draft-mtp");
}

TEST_F(ModelsIniTest, GetPreset_SpecTypeUnderscore)
{
	loadIni("[model]\nmodel = /m.gguf\nspec_type = ngram-simple\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	EXPECT_EQ(preset->load.specType, "ngram-simple");
}

TEST_F(ModelsIniTest, SavePreset_SpecTypeWritten)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "spectest";
	preset.model = "/m.gguf";
	preset.load.specType = "draft-mtp";
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("spectest");
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->load.specType, "draft-mtp");
}

TEST_F(ModelsIniTest, GetPreset_SpecTypeDflashRead)
{
	loadIni("[model]\nmodel = /m.gguf\nspec-type = draft-dflash\n");
	auto preset = ModelsIni::instance().getPreset("model");
	ASSERT_TRUE(preset.has_value());
	EXPECT_EQ(preset->load.specType, "draft-dflash");
}

TEST_F(ModelsIniTest, SavePreset_SpecTypeDflashWritten)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "specdflash";
	preset.model = "/m.gguf";
	preset.load.specType = "draft-dflash";
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("specdflash");
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->load.specType, "draft-dflash");
}

TEST_F(ModelsIniTest, SavePreset_SpecTypeNoneWritten)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "specnone";
	preset.model = "/m.gguf";
	preset.load.specType = "none";
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("specnone");
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->load.specType, "none");
}

TEST_F(ModelsIniTest, SavePreset_SpecTypeEmptyOmitted)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "specempty";
	preset.model = "/m.gguf";
	preset.load.specType = "";
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	// Read back raw INI to confirm key absent
	std::ifstream f(ModelsIni::instance().getPath());
	std::string content((std::istreambuf_iterator<char>(f)),
						 std::istreambuf_iterator<char>());
	EXPECT_EQ(content.find("spec-type"), std::string::npos);
}

// =============================================================================
// kv-unified write test
// =============================================================================

TEST_F(ModelsIniTest, SavePreset_KvUnifiedFalseWritten)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "kvtest";
	preset.model = "/m.gguf";
	preset.load.kvUnified = false;
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("kvtest");
	ASSERT_TRUE(loaded.has_value());
	EXPECT_FALSE(loaded->load.kvUnified);
}

TEST_F(ModelsIniTest, SavePreset_KvUnifiedTrueWritten)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "kvtest2";
	preset.model = "/m.gguf";
	preset.load.kvUnified = true;
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("kvtest2");
	ASSERT_TRUE(loaded.has_value());
	EXPECT_TRUE(loaded->load.kvUnified);
}

// =============================================================================
// Draft settings emit + parse tests (issue #87)
// =============================================================================

TEST_F(ModelsIniTest, SavePreset_DraftSettings_EmittedAndParsed)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "drafttest";
	preset.model = "/m.gguf";
	preset.load.cacheTypeKDraft = "q8_0";
	preset.load.cacheTypeVDraft = "q5_1";
	preset.load.deviceDraft     = "CUDA0,CUDA1";
	preset.load.preserveThinking = true;
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	auto loaded = ModelsIni::instance().getPreset("drafttest");
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->load.cacheTypeKDraft, "q8_0");
	EXPECT_EQ(loaded->load.cacheTypeVDraft, "q5_1");
	EXPECT_EQ(loaded->load.deviceDraft, "CUDA0,CUDA1");
	EXPECT_TRUE(loaded->load.preserveThinking);
}

TEST_F(ModelsIniTest, SavePreset_DraftDefaults_OmittedFromIni)
{
	loadIni("");
	Config::ModelPreset preset;
	preset.name = "draftdefaults";
	preset.model = "/m.gguf";
	// All defaults: cacheTypeKDraft="f16", deviceDraft="", preserveThinking=false
	EXPECT_TRUE(ModelsIni::instance().savePreset(preset));

	std::ifstream f(ModelsIni::instance().getPath());
	std::string content((std::istreambuf_iterator<char>(f)),
					 std::istreambuf_iterator<char>());
	// Default values should be omitted from INI
	EXPECT_EQ(content.find("cache-type-k-draft"), std::string::npos);
	EXPECT_EQ(content.find("device-draft"), std::string::npos);
	EXPECT_EQ(content.find("chat-template-kwargs"), std::string::npos);
}

TEST_F(ModelsIniTest, ParsePreset_PreserveThinkingSubstringMatch)
{
	loadIni("[pttest]\nmodel = /m.gguf\n"
			"chat-template-kwargs = {\"preserve_thinking\": true}\n");
	auto preset = ModelsIni::instance().getPreset("pttest");
	ASSERT_TRUE(preset.has_value());
	EXPECT_TRUE(preset->load.preserveThinking);
}

TEST_F(ModelsIniTest, ParsePreset_PreserveThinkingFalse)
{
	loadIni("[ptfalse]\nmodel = /m.gguf\n"
			"chat-template-kwargs = {\"preserve_thinking\": false}\n");
	auto preset = ModelsIni::instance().getPreset("ptfalse");
	ASSERT_TRUE(preset.has_value());
	EXPECT_FALSE(preset->load.preserveThinking);
}
