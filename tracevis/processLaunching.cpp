#pragma once 
#include <stdafx.h>
#include "processLaunching.h"
#include "GUIManagement.h"
#include "serialise.h"

//for each live process we have a thread rendering graph data for previews, heatmaps and conditionals
//+ module data and disassembly
THREAD_POINTERS *launch_new_process_threads(PID_TID PID, std::map<PID_TID, PROCESS_DATA *> *glob_piddata_map, HANDLE pidmutex, VISSTATE *clientState, cs_mode bitWidth)
{
	THREAD_POINTERS *processThreads = new THREAD_POINTERS;
	PROCESS_DATA *piddata = new PROCESS_DATA(bitWidth);
	piddata->PID = PID;
	if (clientState->switchProcess)
		clientState->spawnedProcess = piddata;

	if (!obtainMutex(pidmutex, 1038)) return 0;
	glob_piddata_map->insert_or_assign(PID, piddata);
	dropMutex(pidmutex);

	//spawns trace threads + handles module data for process
	module_handler *tPIDThread = new module_handler(PID, 0);
	tPIDThread->clientState = clientState;
	tPIDThread->piddata = piddata;

	rgat_create_thread((LPTHREAD_START_ROUTINE)tPIDThread->ThreadEntry, tPIDThread);
	processThreads->modThread = tPIDThread;
	processThreads->threads.push_back(tPIDThread);

	//handles new disassembly data
	basicblock_handler *tBBHandler = new basicblock_handler(PID, 0, bitWidth);
	tBBHandler->clientState = clientState;
	tBBHandler->piddata = piddata;

	rgat_create_thread((LPTHREAD_START_ROUTINE)tBBHandler->ThreadEntry, tBBHandler);
	processThreads->BBthread = tBBHandler;
	processThreads->threads.push_back(tBBHandler);

	//non-graphical
	if (!clientState->commandlineLaunchPath.empty()) return processThreads;

	//graphics rendering threads for each process here	
	preview_renderer *tPrevThread = new preview_renderer(PID, 0);
	tPrevThread->clientState = clientState;
	tPrevThread->piddata = piddata;

	rgat_create_thread((LPTHREAD_START_ROUTINE)tPrevThread->ThreadEntry, tPrevThread);

	heatmap_renderer *tHeatThread = new heatmap_renderer(PID, 0);
	tHeatThread->clientState = clientState;
	tHeatThread->piddata = piddata;
	tHeatThread->setUpdateDelay(clientState->config->heatmap.delay);

	rgat_create_thread((LPTHREAD_START_ROUTINE)tHeatThread->ThreadEntry, tHeatThread);

	processThreads->heatmapThread = tHeatThread;
	processThreads->threads.push_back(tHeatThread);


	conditional_renderer *tCondThread = new conditional_renderer(PID, 0);
	tCondThread->clientState = clientState;
	tCondThread->piddata = piddata;
	tCondThread->setUpdateDelay(clientState->config->conditional.delay);
	Sleep(200);
	rgat_create_thread((LPTHREAD_START_ROUTINE)tCondThread->ThreadEntry, tCondThread);
	processThreads->conditionalThread = tCondThread;
	processThreads->threads.push_back(tCondThread);

	return processThreads;
}

#ifdef WIN32
void process_coordinator_listener(VISSTATE *clientState, vector<THREAD_POINTERS *> *threadsList)
{
	//todo: posibly worry about pre-existing if pidthreads dont work

	HANDLE hPipe = CreateNamedPipe(L"\\\\.\\pipe\\BootstrapPipe",
		PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_MESSAGE,
		255, 65536, 65536, 0, NULL);

	OVERLAPPED ov = { 0 };
	ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	if (hPipe == INVALID_HANDLE_VALUE)
	{
		cout << "[rgat]CreateNamedPipe failed with error " << GetLastError();
		return;
	}

	DWORD res = 0, bread = 0;
	char buf[40];
	while (!clientState->die)
	{
		bool conFail = ConnectNamedPipe(hPipe, &ov);
		if (conFail)
		{
			cerr << "[rgat]Warning! Bootstrap connection error" << endl;
			Sleep(1000);
			continue;
		}

		int err = GetLastError();
		if (err == ERROR_IO_PENDING || err == ERROR_PIPE_LISTENING) {
			res = WaitForSingleObject(ov.hEvent, 3000);
			if (res == WAIT_TIMEOUT) {
				Sleep(100);
				continue;
			}
		}

		ReadFile(hPipe, buf, 30, &bread, NULL);
		DisconnectNamedPipe(hPipe);

		if (!bread) {
			cout << "[rgat]ERROR: Read 0 when waiting for PID. Try again" << endl;
			Sleep(1000);
			continue;
		}
		buf[bread] = 0;

		PID_TID PID = 0;
		cs_mode bitWidth = extract_pid_bitwidth(buf, string("PID"), &PID);
		if (bitWidth)
		{
			clientState->timelineBuilder->notify_new_pid(PID);
			THREAD_POINTERS *threadPointers = launch_new_process_threads(PID, &clientState->glob_piddata_map, clientState->pidMapMutex, clientState, bitWidth);
			threadsList->push_back(threadPointers);
			continue;
		}

	}
}
#endif // WIN32

//listens for new and dying processes, spawns and kills threads to handle them
void process_coordinator_thread(VISSTATE *clientState)
{

	vector<THREAD_POINTERS *> threadsList;
	process_coordinator_listener(clientState, &threadsList);
	if (threadsList.empty()) return;

	//we get here when rgat is exiting
	//this tells all the child threads to die
	vector<THREAD_POINTERS *>::iterator processIt;
	for (processIt = threadsList.begin(); processIt != threadsList.end(); ++processIt)
	{
		THREAD_POINTERS *p = ((THREAD_POINTERS *)*processIt);
		vector<base_thread *>::iterator threadIt = p->threads.begin();
		for (; threadIt != p->threads.end(); ++threadIt)
		{
			//killing BB thread frees the disassembly data, causing race
			if (*threadIt == p->BBthread) continue;
			((base_thread *)*threadIt)->kill();
		}
	}

	//wait for all children to terminate
	for (processIt = threadsList.begin(); processIt != threadsList.end(); ++processIt)
	{
		THREAD_POINTERS *p = ((THREAD_POINTERS *)*processIt);
		vector<base_thread *>::iterator threadIt = p->threads.begin();

		for (; threadIt != p->threads.end(); ++threadIt)
		{
			int waitLimit = 100;
			while (true)
			{
				if (!waitLimit--) ExitProcess(-1);
				if (((base_thread *)*threadIt)->is_alive()) {
					Sleep(2);
					continue;
				}
				break;
			}
		}
	}

	//now safe to kill the disassembler threads
	for (processIt = threadsList.begin(); processIt != threadsList.end(); ++processIt)
		((THREAD_POINTERS *)*processIt)->BBthread->kill();

	for (processIt = threadsList.begin(); processIt != threadsList.end(); ++processIt)
		while (((THREAD_POINTERS *)*processIt)->BBthread->is_alive())
			Sleep(1);

	clientState->glob_piddata_map.clear();
}

//for each saved process we have a thread rendering graph data for previews, heatmaps and conditonals
void launch_saved_process_threads(PID_TID PID, PROCESS_DATA *piddata, VISSTATE *clientState)
{
	preview_renderer *previews_thread = new preview_renderer(PID, 0);
	previews_thread->clientState = clientState;
	previews_thread->piddata = piddata;
	rgat_create_thread((LPTHREAD_START_ROUTINE)previews_thread->ThreadEntry, previews_thread);

	heatmap_renderer *heatmap_thread = new heatmap_renderer(PID, 0);
	heatmap_thread->clientState = clientState;
	heatmap_thread->piddata = piddata;
	rgat_create_thread((LPTHREAD_START_ROUTINE)heatmap_thread->ThreadEntry, heatmap_thread);

	conditional_renderer *conditional_thread = new conditional_renderer(PID, 0);
	conditional_thread->clientState = clientState;
	conditional_thread->piddata = piddata;
	Sleep(200);
	rgat_create_thread((LPTHREAD_START_ROUTINE)conditional_thread->ThreadEntry, conditional_thread);

	clientState->spawnedProcess = clientState->glob_piddata_map[PID];
}


bool loadTrace(VISSTATE *clientState, string filename)
{
	display_only_status_message("Loading save file...", clientState);
	cout << "Reading " << filename << " from disk" << endl;

	FILE* pFile;
	fopen_s(&pFile, filename.c_str(), "rb");
	char buffer[65536];
	rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
	rapidjson::Document saveJSON;
	saveJSON.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);

	//load process data
	string s1;

	rapidjson::Value::ConstMemberIterator PIDIt = saveJSON.FindMember("PID");
	if (PIDIt == saveJSON.MemberEnd())
	{
		cout << "[rgat]ERROR: Process data load failed" << endl;
		return false;
	}
	PID_TID PID = PIDIt->value.GetUint64();

	if (clientState->glob_piddata_map.count(PID)) { cout << "[rgat]PID " << PID << " already loaded! Close rgat and reload" << endl; return false; }
	else
		cout << "[rgat]Loading saved PID: " << PID << endl;

	PROCESS_DATA *newpiddata;
	if (!loadProcessData(clientState, saveJSON, &newpiddata, PID))
	{
		cout << "[rgat]ERROR: Process data load failed" << endl;
		return false;
	}


	cout << "[rgat]Loaded process data. Loading graphs..." << endl;

	if (!loadProcessGraphs(clientState, saveJSON, newpiddata))
	{
		cout << "[rgat]Process Graph load failed" << endl;
		return false;
	}

	cout << "[rgat]Loading completed successfully" << endl;
	fclose(pFile);

	if (!obtainMutex(clientState->pidMapMutex, 1039))
	{
		cerr << "[rgat]ERROR: Failed to obtain pidMapMutex in load" << endl;
		return false;
	}
	clientState->glob_piddata_map[PID] = newpiddata;
	TraceVisGUI *widgets = (TraceVisGUI *)clientState->widgets;
	widgets->addPID(PID);
	dropMutex(clientState->pidMapMutex);

	launch_saved_process_threads(PID, newpiddata, clientState);
	return true;
}

void openSavedTrace(VISSTATE *clientState, void *widgets)
{
	((TraceVisGUI *)widgets)->exeSelector->hide();

	if (!fileExists(clientState->config->saveDir))
	{
		string newSavePath = getModulePath() + "\\saves\\";
		clientState->config->updateSavePath(newSavePath);
	}
	
	ALLEGRO_FILECHOOSER *fileDialog;
	//bug: sometimes uses current directory
	fileDialog = al_create_native_file_dialog(clientState->config->saveDir.c_str(),
		"Choose saved trace to open", "*.rgat;*.*;",
		ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);

	clientState->openFrames.push_back((agui::Frame *) 0);
	al_show_native_file_dialog(clientState->maindisplay, fileDialog);
	clientState->openFrames.clear();

	const char* result = al_get_native_file_dialog_path(fileDialog, 0);
	al_destroy_native_file_dialog(fileDialog);
	if (!result) return;

	string path(result);
	if (!fileExists(path)) return;

	loadTrace(clientState, path);
	clientState->modes.animation = false;
}

void saveTraces(VISSTATE *clientState)
{
	if (!clientState->activeGraph) return;

	stringstream displayMessage;
	PID_TID pid = ((plotted_graph *)clientState->activeGraph)->get_pid();
	displayMessage << "[rgat]Starting save of process " << pid << " to filesystem" << endl;
	display_only_status_message("Saving process " + to_string(pid), clientState);
	cout << displayMessage.str();
	saveTrace(clientState);
}