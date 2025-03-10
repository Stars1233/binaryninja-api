//
// Created by kat on 8/6/24.
//

// TODO We could use an LLIL/MLIL workflow to rewrite off-image value-loads
//  	(i.e. MLIL_VAR_LOAD.MLIL_DEREF.MLIL_CONST_PTR) to just read the value out of the cache and replace the load
// 		in stub regions.
//
// This is a pretty rough workflow and has huge room for improvements all around.

#include "SharedCacheWorkflow.h"
#include "lowlevelilinstruction.h"
#include "mediumlevelilinstruction.h"
#include "../api/sharedcacheapi.h"
#include "thread"
#include <shared_mutex>

#include "ObjCActivity.h"

using namespace BinaryNinja;
using namespace SharedCacheAPI;

struct GlobalWorkflowState
{
	// Mutex to guard against duplicate region/image loads that cause reanalysis.
	std::mutex loadMutex;
	bool autoLoadStubsAndDyldData = true;
	bool autoLoadObjCStubRequirements = true;
};

std::shared_ptr<GlobalWorkflowState> GetGlobalWorkflowState(Ref<BinaryView> view)
{
	static std::shared_mutex globalWorkflowStateMutex;
	static std::unordered_map<uint64_t, std::shared_ptr<GlobalWorkflowState>> globalWorkflowState;

	std::shared_lock<std::shared_mutex> readLock(globalWorkflowStateMutex);
	const uint64_t viewId = view->GetFile()->GetSessionId();
	auto foundState = globalWorkflowState.find(viewId);
	if (foundState != globalWorkflowState.end())
		return foundState->second;
	readLock.unlock();

	std::unique_lock<std::shared_mutex> writeLock(globalWorkflowStateMutex);
	globalWorkflowState[viewId] = std::make_shared<GlobalWorkflowState>();
	Ref<Settings> settings = view->GetLoadSettings(VIEW_NAME);

	bool autoLoadStubsAndDyldData = true;
	if (settings && settings->Contains("loader.dsc.autoLoadStubsAndDyldData"))
		autoLoadStubsAndDyldData = settings->Get<bool>("loader.dsc.autoLoadStubsAndDyldData", view);
	globalWorkflowState[viewId]->autoLoadStubsAndDyldData = autoLoadStubsAndDyldData;

	bool autoLoadObjC = true;
	if (settings && settings->Contains("loader.dsc.autoLoadObjCStubRequirements"))
		autoLoadObjC = settings->Get<bool>("loader.dsc.autoLoadObjCStubRequirements", view);
	globalWorkflowState[viewId]->autoLoadObjCStubRequirements = autoLoadObjC;

	return globalWorkflowState[viewId];
}

// TODO: Add a type library cache to this workflow. (so we dont take global file lock)
Ref<TypeLibrary> TypeLibraryFromName(BinaryView& view, const std::string& name) {
	// Check to see if we have already loaded the type library.
	if (auto typeLib = view.GetTypeLibrary(name))
		return typeLib;

	// TODO: Use the functions platform instead.
	auto typeLibs = view.GetDefaultPlatform()->GetTypeLibrariesByName(name);
	if (!typeLibs.empty())
		return typeLibs.front();
	return nullptr;
}

void IdentifyStub(BinaryView& view, const SharedCacheController& controller, uint64_t stubFuncAddr, uint64_t symbolAddr) {
	static const char* STUB_PREFIX = "j_";
	// TODO: Just check for if there is a user symbol instead?
	// TODO: ^ well we really should be using an auto symbol no?
	// Check to see if there is a symbol already at the target. If so we should just stop.
	if (symbolAddr == stubFuncAddr)
		if (const auto targetSymbol = view.GetSymbolByAddress(stubFuncAddr))
			if (targetSymbol->GetShortName().find(STUB_PREFIX) != std::string::npos)
				return;

	// Try and apply a version of the symbol address to the target address
	if (symbolAddr != stubFuncAddr)
	{
		if (const auto symbol = view.GetSymbolByAddress(symbolAddr))
		{
			// A symbol already exists at the source location. Add a stub symbol at `targetLocation` based on the existing symbol.
			const auto id = view.BeginUndoActions();
			if (auto targetFunc = view.GetAnalysisFunction(view.GetDefaultPlatform(), stubFuncAddr))
				view.DefineUserSymbol(new Symbol(FunctionSymbol, STUB_PREFIX + symbol->GetShortName(), stubFuncAddr));
			else
				view.DefineUserSymbol(new Symbol(symbol->GetType(), STUB_PREFIX + symbol->GetShortName(), stubFuncAddr));
			view.ForgetUndoActions(id);
			return;
		}
	}

	// No existing symbol located, try and search through the symbols of the cache.
	auto symbol = controller.GetSymbolAt(symbolAddr);
	if (!symbol.has_value())
		return;

	// NOTE: The type library name is expected to be the image name currently.
	// Try and pull the type from the associated type library (if there is one)
	Ref<Type> type = nullptr;
	if (const auto image = controller.GetImageContaining(symbolAddr))
		if (auto typeLib = TypeLibraryFromName(view, image->name))
			type = view.ImportTypeLibraryObject(typeLib, {symbol->name});

	// Define the symbol and type (if found)
	auto targetFunc = view.GetAnalysisFunction(view.GetDefaultPlatform(), stubFuncAddr);
	if (type && targetFunc)
		targetFunc->SetUserType(type);
	// TODO: When to reanalysis function (mark updates required?)
	view.DefineUserSymbol(new Symbol(symbol->type, STUB_PREFIX + symbol->name, stubFuncAddr));
}

// Loads the associated image or region for the specified target address. Returning if it was loaded or not.
bool TryLoadTarget(BinaryView& view, SharedCacheController& controller, const uint64_t target) {
	if (const auto image = controller.GetImageContaining(target))
		return controller.ApplyImage(view, *image);
	// Failed to find image, try and apply region instead.
	if (const auto region = controller.GetRegionContaining(target))
		return controller.ApplyRegion(view, *region);
	return false;
};

void FixupStubs(Ref<AnalysisContext> ctx)
{
	const auto func = ctx->GetFunction();
	const auto view = func->GetView();
	const auto mlil = ctx->GetMediumLevelILFunction();
	if (!mlil)
		return;
	const auto mssa = mlil->GetSSAForm();
	if (!mssa)
		return;

	auto workflowState = GetGlobalWorkflowState(view);
	auto controller = SharedCacheController::GetController(*view);
	if (!controller)
		return;

	// Get the containing section for section specific tasks.
	auto funcStart = func->GetStart();
	auto sections = view->GetSectionsAt(funcStart);
	if (sections.empty())
		return;
	const auto& section = sections.front();
	const auto sectionName = section->GetName();

	// Load the target region if applicable and then trigger re-analysis for all functions in our section.
	auto processStubImageCall = [&](const uint64_t target) {
		// TODO: If this fails that doesn't necessarily mean we are duplicating work and thus allowing early return...
		if (!workflowState->loadMutex.try_lock())
			return;

		if (!view->IsValidOffset(target) && TryLoadTarget(*view, *controller, target))
		{
			// Update all the functions inside our current activities function section.
			for (const auto &sectFunc : view->GetAnalysisFunctionList())
				if (section->GetStart() <= sectFunc->GetStart() && sectFunc->GetStart() < section->GetEnd())
					sectFunc->Reanalyze();
		}

		workflowState->loadMutex.unlock();
	};

	// TODO: Split this out into another function.
	// Processor that automatically loads the libObjC image when it encounters a stub (so we can do inlining).
	if (workflowState->autoLoadObjCStubRequirements && sectionName.find("__objc_stubs") != std::string::npos)
	{
		auto firstInstruction = mlil->GetInstruction(0);
		if (firstInstruction.operation == MLIL_TAILCALL)
		{
			auto dest = firstInstruction.GetDestExpr<MLIL_TAILCALL>();
			if (dest.operation == MLIL_CONST_PTR)
			{
				// We're ready, everything is here
				func->SetAutoInlinedDuringAnalysis(true);
				return;
			}
		}

		for (const auto& block : mssa->GetBasicBlocks())
		{
			for (size_t i = block->GetStart(), end = block->GetEnd(); i < end; ++i)
			{
				auto instr = mssa->GetInstruction(i);
				if (instr.operation == MLIL_JUMP)
				{
					if (instr.GetDestExpr<MLIL_JUMP>().operation == MLIL_VAR_SSA)
					{
						auto dest = instr.GetDestExpr<MLIL_JUMP>();
						auto value = mssa->GetSSAVarValue(dest.GetSourceSSAVariable());
						if (value.state == UndeterminedValue)
						{
							auto def = mssa->GetSSAVarDefinition(dest.GetSourceSSAVariable());
							auto defInstr = mssa->GetInstruction(def);
							auto targetOffset = defInstr.GetSourceExpr().GetSourceExpr().GetConstant();
							processStubImageCall(targetOffset);
						}
					}
					else if (instr.GetDestExpr<MLIL_JUMP>().operation == MLIL_CONST_PTR)
					{
						auto dest = instr.GetDestExpr<MLIL_JUMP>();
						auto targetOffset = dest.GetConstant();
						processStubImageCall(targetOffset);
					}
				}
			}
		}

		return;
	}

	// TODO: Split this out into another function.
	// If this is a stub function we should map in the called region / image.
	// NOTE: We cant use the region type here as these sections will be found under larger image region
	// "_stubs" => Branch Islands / Stubs (iOS 16 / macOs)
	// "dyld_shared_cache_branch_islands" => Branch Islands (iOS 11-?)
	if (sectionName.rfind("_stubs") != std::string::npos || sectionName.rfind("dyld_shared_cache_branch_islands") != std::string::npos)
	{
		// Stage 0: Load the jumped to region, so that x16 is resolved.
		// 0 @ 180359b58  (MLIL_SET_VAR.q x16 = (MLIL_LOAD.q [(MLIL_CONST_PTR.q &data_1eacf40f8)].q))
		// 1 @ 180359b5c  (MLIL_JUMP jump((MLIL_VAR.q x16)))
		for (const auto& block : mssa->GetBasicBlocks())
		{
			for (size_t i = block->GetStart(), end = block->GetEnd(); i < end; ++i)
			{
				auto instr = mssa->GetInstruction(i);
				if (instr.operation == MLIL_JUMP)
				{
					if (instr.GetDestExpr<MLIL_JUMP>().operation == MLIL_VAR_SSA)
					{
						auto dest = instr.GetDestExpr<MLIL_JUMP>();
						auto value = mssa->GetSSAVarValue(dest.GetSourceSSAVariable());
						if (value.state == UndeterminedValue)
						{
							auto def = mssa->GetSSAVarDefinition(dest.GetSourceSSAVariable());
							auto defInstr = mssa->GetInstruction(def);
							auto targetOffset = defInstr.GetSourceExpr().GetSourceExpr().GetConstant();
							processStubImageCall(targetOffset);
						}
					}
				}
			}
		}
	}
}

// TODO: FixupOffImageAccess
void FixupOffImageCalls(Ref<AnalysisContext> ctx)
{
	const auto func = ctx->GetFunction();
	const auto view = func->GetView();
	const auto mlil = ctx->GetMediumLevelILFunction();
	if (!mlil)
		return;
	const auto mssa = mlil->GetSSAForm();
	if (!mssa)
		return;

	auto workflowState = GetGlobalWorkflowState(view);
	auto controller = SharedCacheController::GetController(*view);
	if (!controller)
		return;

	auto processOffImageCall = [&](const uint64_t stubFuncAddr, const uint64_t symbolAddr) {
		const auto region = controller->GetRegionContaining(symbolAddr);
		if (!region.has_value())
			return;

		// Load stub region if not already loaded and reanalyze the function (to pickup stub functions)
		if (workflowState->autoLoadStubsAndDyldData && region->type == SharedCacheRegionTypeStubIsland)
			if (controller->ApplyRegion(*view, *region))
				func->Reanalyze(); // TODO: Call mark updates required instead??? Use incremental update type instead???

		// TODO: Is there a point to this at all????
		// TODO: is there a point to calling this if workflowState->autoLoadStubsAndDyldData is not on???
		// Apply symbols (and possibly a type) to the relevant stub functions
		IdentifyStub(*view, *controller, stubFuncAddr, symbolAddr);
	};

	// Load all unmapped STUB regions / images that are called in this function.
	for (const auto& block : mssa->GetBasicBlocks())
	{
		for (size_t i = block->GetStart(), end = block->GetEnd(); i < end; ++i)
		{
			auto instr = mssa->GetInstruction(i);
			if (instr.operation == MLIL_CALL_SSA)
			{
				if (instr.GetDestExpr<MLIL_CALL_SSA>().operation == MLIL_CONST_PTR)
				{
					auto targetAddr = instr.GetDestExpr<MLIL_CALL_SSA>().GetConstant();
					if (!view->IsValidOffset(targetAddr))
					{
						processOffImageCall(targetAddr, targetAddr);
					}
				}
			}
			else if (instr.operation == MLIL_JUMP)
			{
				auto destExpr = instr.GetDestExpr<MLIL_JUMP>();
				if (destExpr.operation == MLIL_VAR_SSA)
				{
					// 1 @ 180359b5c  (MLIL_JUMP jump((MLIL_VAR.q x16)))
					auto dest = instr.GetDestExpr<MLIL_JUMP>();
					auto value = mssa->GetSSAVarValue(dest.GetSourceSSAVariable());
					if (value.state == UndeterminedValue)
					{
						auto def = mssa->GetSSAVarDefinition(dest.GetSourceSSAVariable());
						auto defInstr = mssa->GetInstruction(def);
						if (defInstr.operation == MLIL_SET_VAR_SSA)
						{
							// 0 @ 180359b58  (MLIL_SET_VAR.q x16 = (MLIL_LOAD.q [(MLIL_CONST_PTR.q &data_1eacf40f8)].q))
							auto loadExpr = defInstr.GetSourceExpr<MLIL_SET_VAR_SSA>();
							if (loadExpr.operation == MLIL_LOAD_SSA)
							{
								auto ptrExpr = loadExpr.GetSourceExpr<MLIL_LOAD_SSA>();
								if (ptrExpr.operation == MLIL_CONST_PTR)
								{
									auto targetAddr = ptrExpr.GetConstant();
									if (!view->IsValidOffset(targetAddr))
									{
										processOffImageCall(targetAddr, targetAddr);
									}
								}
							}
						}
					}
					else if (destExpr.operation == MLIL_CONST_PTR)
					{
						// 4 @ 18aa08208  (MLIL_JUMP jump((MLIL_CONST_PTR.q 0x18c1369f0)))
						auto targetAddr = destExpr.GetConstant();
						if (!view->IsValidOffset(targetAddr))
						{
							processOffImageCall(targetAddr, targetAddr);
						}
					}
					else if (destExpr.operation == MLIL_LOAD_SSA)
					{
						auto ptrExpr = destExpr.GetSourceExpr<MLIL_LOAD_SSA>();
						if (ptrExpr.operation == MLIL_CONST_PTR)
						{
							auto targetAddr = ptrExpr.GetConstant();
							if (!view->IsValidOffset(targetAddr))
							{
								processOffImageCall(targetAddr, targetAddr);
							}
						}
					}
				}
			}
			// TODO: Check all instructions for accesses to select region types (stub etc...)
			// TODO: ^ we actually dont really need to do this, the other type of access cont..
			// TODO: the other two types of accesses (load & save) we dont want to load their regions, just
			// TODO: their symbol information if available.
		}
	}
}

static constexpr auto WORKFLOW_DESCRIPTION = R"({
  "title": "Shared Cache Workflow",
  "description": "Shared Cache Workflow",
  "capabilities": []
})";

void SharedCacheWorkflow::Register()
{
	Ref<Workflow> workflow = Workflow::Instance("core.function.baseAnalysis")->Clone("core.function.sharedCache");

	// Register and insert activities here.
	ObjCActivity::Register(*workflow);
	workflow->RegisterActivity(new Activity("core.analysis.sharedCache.stubs", &FixupStubs));
	workflow->RegisterActivity(new Activity("core.analysis.sharedCache.calls", &FixupOffImageCalls));
	workflow->Insert("core.function.analyzeTailCalls", "core.analysis.sharedCache.stubs");
	workflow->Insert("core.function.analyzeTailCalls", "core.analysis.sharedCache.calls");

	Workflow::RegisterWorkflow(workflow, WORKFLOW_DESCRIPTION);
}

extern "C"
{
	void RegisterSharedCacheWorkflow()
	{
		SharedCacheWorkflow::Register();
	}
}
