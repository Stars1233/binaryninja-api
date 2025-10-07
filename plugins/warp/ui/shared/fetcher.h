#pragma once

#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <functional>

#include "warp.h"
#include "binaryninjaapi.h"
#include "uitypes.h"

enum WarpFetchCompletionStatus
{
	KeepCallback,
	RemoveCallback,
};

// Responsible for fetching data from the containers, to later be queried from the container interface.
class WarpFetcher
{
	LoggerRef m_logger;

	std::mutex m_requestMutex;
	std::vector<FunctionRef> m_pendingRequests;
	std::unordered_set<Warp::FunctionGUID> m_processedGuids;

	// List of callbacks to call when done fetching data, assume that others are using this as well.
	std::vector<std::function<WarpFetchCompletionStatus()>> m_completionCallbacks;

public:
	explicit WarpFetcher();

	// The global fetcher instance, this is used for the fetch dialog and the sidebar.
	static std::shared_ptr<WarpFetcher> Global();

	std::atomic<bool> m_requestInProgress = false;

	void AddCompletionCallback(std::function<WarpFetchCompletionStatus()> cb)
	{
		std::lock_guard<std::mutex> lock(m_requestMutex);
		m_completionCallbacks.push_back(std::move(cb));
	}

	void AddPendingFunction(const FunctionRef& func);

	void FetchPendingFunctions(const std::vector<Warp::SourceTag>& allowedTags);

	void ClearProcessed();

private:
	std::vector<FunctionRef> FlushPendingFunctions();

	void ExecuteCompletionCallback();
};
