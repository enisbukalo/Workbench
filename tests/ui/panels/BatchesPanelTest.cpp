/**
 * @file BatchesPanelTest.cpp
 * @brief Unit tests for BatchesPanel using mocked dependencies.
 */

#include "batchesPanel.h"

#include "MockBatchStateTracker.h"
#include "MockBatchStore.h"
#include "MockConfigManager.h"
#include "MockCpuMonitor.h"
#include "MockGpuMonitor.h"
#include "MockLlamaServerProcess.h"
#include "MockMemoryMonitor.h"
#include "MockModelInfoMonitor.h"
#include "MockModelStateTracker.h"
#include "MockModelsIni.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

using namespace testing;
using namespace ftxui;

class BatchesPanelTest : public Test
{
  protected:
	AppDependencies makeDeps()
	{
		return AppDependencies{ mockConfig,	  mockServer,	mockModelInfo,
								mockTracker,  mockModelsIni, mockBatches,
								mockBatchTracker, mockCpu,	mockMem,
								mockGpu };
	}

	// --- friend-access seams onto private editor state/actions ---
	void startNew(BatchesPanel &p) { p.startNewBatch(); }
	void setCount(BatchesPanel &p, int n) { p.setPresetCount(n); }
	void setSlot(BatchesPanel &p, int i, const std::string &s)
	{
		p.m_editPresets[i] = s;
	}
	void save(BatchesPanel &p) { p.saveEditedBatch(); }
	void del(BatchesPanel &p) { p.deleteSelectedBatch(); }
	void loadBatch(BatchesPanel &p) { p.loadSelectedBatch(); }
	void unloadBatch(BatchesPanel &p) { p.unloadSelectedBatch(); }
	void clickLoadUnload(BatchesPanel &p) { p.onLoadUnloadClicked(); }
	void loadBatchIntoEditor(BatchesPanel &p) { p.loadSelectedIntoEditor(); }
	void refreshLabel(BatchesPanel &p) { p.updateLoadLabel(); }
	std::string loadLabel(BatchesPanel &p) const { return p.m_loadLabel; }
	int editCount(BatchesPanel &p) const { return p.m_editCount; }
	void setEditName(BatchesPanel &p, const std::string &n) { p.m_editName = n; }
	void setSelected(BatchesPanel &p, int i) { p.m_selectedBatchIndex = i; }
	const std::vector<std::string> &editPresets(BatchesPanel &p) const
	{
		return p.m_editPresets;
	}

	NiceMock<MockConfigManager> mockConfig;
	NiceMock<MockLlamaServerProcess> mockServer;
	NiceMock<MockModelInfoMonitor> mockModelInfo;
	NiceMock<MockModelStateTracker> mockTracker;
	NiceMock<MockModelsIni> mockModelsIni;
	NiceMock<MockBatchStore> mockBatches;
	NiceMock<MockBatchStateTracker> mockBatchTracker;
	NiceMock<MockCpuMonitor> mockCpu;
	NiceMock<MockMemoryMonitor> mockMem;
	NiceMock<MockGpuMonitor> mockGpu;
};

TEST_F(BatchesPanelTest, ComponentReturnsValidElement)
{
	auto deps = makeDeps();
	BatchesPanel panel(deps);
	ASSERT_TRUE(panel.component());
}

TEST_F(BatchesPanelTest, CountIncrease_AppendsFirstUnusedRows)
{
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b", "c" }));
	auto deps = makeDeps();
	BatchesPanel panel(deps);

	startNew(panel); // count 1, first unused row ("a")
	EXPECT_EQ(editCount(panel), 1);
	EXPECT_EQ(editPresets(panel), (std::vector<std::string>{ "a" }));

	// Grow: each new row defaults to the first preset not already used, so the
	// rows stay distinct (no preset can appear in a batch twice).
	setCount(panel, 3);
	EXPECT_EQ(editCount(panel), 3);
	EXPECT_EQ(editPresets(panel), (std::vector<std::string>{ "a", "b", "c" }));
}

TEST_F(BatchesPanelTest, CountDecrease_RemovesFromBottom_KeepsOthers)
{
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b", "c" }));
	auto deps = makeDeps();
	BatchesPanel panel(deps);

	startNew(panel);
	setCount(panel, 3);
	setSlot(panel, 0, "a");
	setSlot(panel, 1, "b");
	setSlot(panel, 2, "c");

	setCount(panel, 2); // drop from the bottom only
	EXPECT_EQ(editCount(panel), 2);
	EXPECT_EQ(editPresets(panel), (std::vector<std::string>{ "a", "b" }));
}

TEST_F(BatchesPanelTest, Count_ClampedToDistinctPresetCount)
{
	// Only two presets exist -> a batch can have at most two rows.
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b" }));
	auto deps = makeDeps();
	BatchesPanel panel(deps);
	startNew(panel);

	setCount(panel, 0);
	EXPECT_EQ(editCount(panel), 1); // min
	setCount(panel, 99);
	EXPECT_EQ(editCount(panel), 2); // capped at #presets
}

TEST_F(BatchesPanelTest, SaveBatch_PersistsDistinctRowsInOrder)
{
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b", "c" }));

	Batch saved;
	EXPECT_CALL(mockBatches, saveBatch(_))
		.WillOnce(DoAll(SaveArg<0>(&saved), Return(true)));

	auto deps = makeDeps();
	BatchesPanel panel(deps);

	startNew(panel);
	setEditName(panel, "mybatch");
	setCount(panel, 3); // -> a, b, c
	save(panel);

	EXPECT_EQ(saved.name, "mybatch");
	EXPECT_EQ(saved.presets, (std::vector<std::string>{ "a", "b", "c" }));
}

TEST_F(BatchesPanelTest, LoadIntoEditor_SetsCountToPresetSize)
{
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b" }));
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "b" } }));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	panel.component(); // build so menu on_change wiring exists
	// Drive the editor-load directly.
	loadBatchIntoEditor(panel);
	EXPECT_EQ(editCount(panel), 2);
	EXPECT_EQ(editPresets(panel), (std::vector<std::string>{ "a", "b" }));
}

TEST_F(BatchesPanelTest, SaveBatch_EmptyName_DoesNotPersist)
{
	EXPECT_CALL(mockBatches, saveBatch(_)).Times(0);

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	startNew(panel);
	setEditName(panel, "");
	save(panel);
}

TEST_F(BatchesPanelTest, DeleteBatch_CallsStore)
{
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke", "prod" }));
	EXPECT_CALL(mockBatches, deleteBatch("smoke")).WillOnce(Return(true));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0); // "smoke"
	del(panel);
}

TEST_F(BatchesPanelTest, LoadBatch_LoadsEachPresetInOrder)
{
	ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b" }));
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "b" } }));

	InSequence seq;
	EXPECT_CALL(mockTracker, requestLoad("a"));
	EXPECT_CALL(mockServer, loadModel("a")).WillOnce(Return(true));
	EXPECT_CALL(mockTracker, requestLoad("b"));
	EXPECT_CALL(mockServer, loadModel("b")).WillOnce(Return(true));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	loadBatch(panel);
}

TEST_F(BatchesPanelTest, LoadBatch_SkipsUnknownPreset)
{
	ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a" })); // "ghost" absent
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "ghost" } }));

	EXPECT_CALL(mockServer, loadModel("a")).WillOnce(Return(true));
	EXPECT_CALL(mockServer, loadModel("ghost")).Times(0);

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	loadBatch(panel);
}

TEST_F(BatchesPanelTest, LoadBatch_ServerStopped_NoLoadCalls)
{
	ON_CALL(mockServer, isRunning()).WillByDefault(Return(false));
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a" } }));

	EXPECT_CALL(mockServer, loadModel(_)).Times(0);

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	loadBatch(panel);
}

TEST_F(BatchesPanelTest, UnloadBatch_UnloadsEachPreset)
{
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "b" } }));

	EXPECT_CALL(mockTracker, requestUnload("a"));
	EXPECT_CALL(mockServer, unloadModel("a")).WillOnce(Return(true));
	EXPECT_CALL(mockTracker, requestUnload("b"));
	EXPECT_CALL(mockServer, unloadModel("b")).WillOnce(Return(true));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	unloadBatch(panel);
}

TEST_F(BatchesPanelTest, Render_ShowsBatchNames)
{
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke", "prod" }));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	auto comp = panel.component();
	ASSERT_TRUE(comp);

	Screen screen(120, 40);
	Render(screen, comp->Render());
	const std::string out = screen.ToString();
	EXPECT_NE(out.find("smoke"), std::string::npos);
	EXPECT_NE(out.find("prod"), std::string::npos);
}

TEST_F(BatchesPanelTest, BottomButton_Unloaded_LabelLoad)
{
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a" } }));
	ON_CALL(mockBatchTracker, statusOf(_, _))
		.WillByDefault(Return(BatchLifecycle::UNLOADED));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	refreshLabel(panel);
	EXPECT_EQ(loadLabel(panel), "LOAD");
}

TEST_F(BatchesPanelTest, BottomButton_Loaded_LabelUnload)
{
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a" } }));
	ON_CALL(mockBatchTracker, statusOf(_, _))
		.WillByDefault(Return(BatchLifecycle::LOADED));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	refreshLabel(panel);
	EXPECT_EQ(loadLabel(panel), "UNLOAD");
}

TEST_F(BatchesPanelTest, BottomButton_Partial_LabelPartiallyLoaded)
{
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a" } }));
	ON_CALL(mockBatchTracker, statusOf(_, _))
		.WillByDefault(Return(BatchLifecycle::LOADING));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	refreshLabel(panel);
	EXPECT_EQ(loadLabel(panel), "Partially Loaded");
}

TEST_F(BatchesPanelTest, ClickPartiallyLoaded_LoadsRemaining)
{
	ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b" }));
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "b" } }));
	// Partial state -> button label "Partially Loaded", click acts like LOAD.
	ON_CALL(mockBatchTracker, statusOf(_, _))
		.WillByDefault(Return(BatchLifecycle::LOADING));
	// "a" already LOADED -> only "b" should load.
	ModelState sa;
	sa.id = "a";
	sa.lifecycle = ModelLifecycle::LOADED;
	ON_CALL(mockTracker, snapshot())
		.WillByDefault(Return(std::map<std::string, ModelState>{ { "a", sa } }));

	EXPECT_CALL(mockServer, loadModel("a")).Times(0);
	EXPECT_CALL(mockServer, loadModel("b")).WillOnce(Return(true));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	refreshLabel(panel); // -> "Partially Loaded"
	clickLoadUnload(panel);
}

TEST_F(BatchesPanelTest, ClickLoad_LoadsAllPresets)
{
	ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b" }));
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "b" } }));
	// Selected batch reports UNLOADED, so the button is in LOAD state.
	ON_CALL(mockBatchTracker, statusOf(_, _))
		.WillByDefault(Return(BatchLifecycle::UNLOADED));
	// Tracker has nothing loaded -> no preset is skipped.
	ON_CALL(mockTracker, snapshot())
		.WillByDefault(Return(std::map<std::string, ModelState>{}));

	EXPECT_CALL(mockServer, loadModel("a")).WillOnce(Return(true));
	EXPECT_CALL(mockServer, loadModel("b")).WillOnce(Return(true));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	refreshLabel(panel); // -> LOAD
	clickLoadUnload(panel);
}

TEST_F(BatchesPanelTest, ClickLoad_SkipsAlreadyLoadedPreset)
{
	ON_CALL(mockServer, isRunning()).WillByDefault(Return(true));
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b" }));
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "b" } }));
	ON_CALL(mockBatchTracker, statusOf(_, _))
		.WillByDefault(Return(BatchLifecycle::LOADING));
	// "a" is already LOADED -> must be skipped; only "b" loads.
	ModelState sa;
	sa.id = "a";
	sa.lifecycle = ModelLifecycle::LOADED;
	ON_CALL(mockTracker, snapshot())
		.WillByDefault(Return(std::map<std::string, ModelState>{ { "a", sa } }));

	EXPECT_CALL(mockServer, loadModel("a")).Times(0);
	EXPECT_CALL(mockServer, loadModel("b")).WillOnce(Return(true));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	loadBatch(panel);
}

TEST_F(BatchesPanelTest, ClickUnload_UnloadsAllPresets)
{
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "b" } }));
	// Selected batch reports LOADED, so the button is in UNLOAD state.
	ON_CALL(mockBatchTracker, statusOf(_, _))
		.WillByDefault(Return(BatchLifecycle::LOADED));

	EXPECT_CALL(mockServer, unloadModel("a")).WillOnce(Return(true));
	EXPECT_CALL(mockServer, unloadModel("b")).WillOnce(Return(true));

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	setSelected(panel, 0);
	refreshLabel(panel); // -> UNLOAD
	clickLoadUnload(panel);
}

TEST_F(BatchesPanelTest, Construct_WithOneBatch_PopulatesEditor)
{
	// Single batch: index is 0 from the start, so MenuOption::on_change never
	// fires. The ctor must still populate the editor from that batch.
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b" }));
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{ "smoke" }));
	ON_CALL(mockBatches, getBatch("smoke"))
		.WillByDefault(Return(Batch{ "smoke", { "a", "b" } }));

	auto deps = makeDeps();
	BatchesPanel panel(deps);

	EXPECT_EQ(editPresets(panel), (std::vector<std::string>{ "a", "b" }));
	EXPECT_EQ(editCount(panel), 2);
}

TEST_F(BatchesPanelTest, Construct_NoBatches_StartsEmptyEditorWithOneRow)
{
	ON_CALL(mockModelsIni, getModelNames())
		.WillByDefault(Return(std::vector<std::string>{ "a", "b" }));
	ON_CALL(mockBatches, getBatchNames())
		.WillByDefault(Return(std::vector<std::string>{}));

	auto deps = makeDeps();
	BatchesPanel panel(deps);

	EXPECT_EQ(editCount(panel), 1);
	EXPECT_EQ(editPresets(panel).size(), 1u);
}

TEST_F(BatchesPanelTest, OnShown_RefreshesPresetListFromIni)
{
	// First read: only "a". After Model Settings adds "b", a later read returns
	// both; onShown() must pick that up so "b" becomes selectable in a slot.
	EXPECT_CALL(mockModelsIni, getModelNames())
		.WillOnce(Return(std::vector<std::string>{ "a" }))			   // ctor
		.WillRepeatedly(Return(std::vector<std::string>{ "a", "b" })); // onShown

	auto deps = makeDeps();
	BatchesPanel panel(deps);
	startNew(panel);
	setCount(panel, 1); // one row -> "a"
	EXPECT_EQ(editPresets(panel), (std::vector<std::string>{ "a" }));

	panel.onShown();
	setCount(panel, 2); // now possible: two distinct presets exist
	EXPECT_EQ(editCount(panel), 2);
	EXPECT_EQ(editPresets(panel), (std::vector<std::string>{ "a", "b" }));
}
