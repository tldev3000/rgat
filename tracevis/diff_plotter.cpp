/*
Copyright 2016 Nia Catlin

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/*
Class for the code that plots graph divergence
*/
#include "stdafx.h"
#include "diff_plotter.h"
#include "plotted_graph_layouts.h"
#include "rendering.h"


diff_plotter::diff_plotter(plotted_graph *g1, plotted_graph *g2, VISSTATE *state)
{


		graph1 = g1;
		graph2 = g2;

	if (g1->getLayout() == eCylinderLayout)
	{
		cout << "creating cylinder diffgraph" << endl;
		diffgraph = new cylinder_graph(0, 0, graph1->get_protoGraph(), &state->config->graphColours);
	}
	else if (g1->getLayout() == eSphereLayout)
	{
		cout << "creating sphere diffgraph" << endl;
		diffgraph = new sphere_graph(0, 0, graph1->get_protoGraph(), &state->config->graphColours);
	}
	else if (g1->getLayout() == eTreeLayout)
		diffgraph = new tree_graph(0, 0, graph1->get_protoGraph(), &state->config->graphColours);
	else
		assert(0);
	diffgraph->main_scalefactors = graph1->main_scalefactors;

	glGenBuffers(4, diffgraph->graphVBOs);
	clientState = state;
}

plotted_graph *diff_plotter::get_graph(int idx) {
	if (idx == 1) return graph1;
	return graph2;
}


void diff_plotter::display_diff_summary(int x, int y, ALLEGRO_FONT *font, VISSTATE *clientState)
{
	stringstream infotxt1, infotxt2, infotxt3;

	string modPath1, modPath2;
	graph1->get_protoGraph()->get_piddata()->get_modpath(0, &modPath1);
	//graph2->get_protoGraph()->get_piddata()->get_modpath(0, &modPath2);

	infotxt1 << "Green - both traces" << endl;
	infotxt2 << "Red - (PID:" << graph1->get_pid() << " TID:" << graph1->get_tid() <<
		") Path: " << modPath1 << "only" << endl;

	int textVSep = font->height + 5;



	if (divergenceFound)
	{
		al_draw_text(font, al_col_orange, x, y, ALLEGRO_ALIGN_LEFT, infotxt1.str().c_str());
		al_draw_text(font, al_col_orange, x, y + textVSep, ALLEGRO_ALIGN_LEFT, infotxt2.str().c_str());
		al_draw_text(font, al_col_orange, x, y + textVSep * 2, ALLEGRO_ALIGN_LEFT, "Divergence found [ESC to reset]");
	}
	else
	{
		al_draw_text(font, al_col_green, x, y, ALLEGRO_ALIGN_LEFT, infotxt1.str().c_str());
		al_draw_text(font, al_col_green, x, y + textVSep, ALLEGRO_ALIGN_LEFT, infotxt2.str().c_str());
		al_draw_text(font, al_col_green, x, y + textVSep * 2, ALLEGRO_ALIGN_LEFT, "No divergence found [ESC to reset]");
	}
}

void diff_plotter::mark_divergence()
{
	divergenceFound = true;
	diffNode = lastNode;
	edgeColour = divergingEdgeColour;
}

NODEPAIR diff_plotter::firstLastNode(MEM_ADDRESS blockAddr, BLOCK_IDENTIFIER blockID, PROCESS_DATA *pd, PID_TID thread)
{
	bool die = false;
	INSLIST *blk = getDisassemblyBlock(blockAddr, blockID, pd, &die);
	if (!blk)
	{
		BB_DATA *foundExtern;
		pd->get_extern_at_address(blockAddr, &foundExtern, 1);
		EDGELIST callingNodes = foundExtern->thread_callers.at(thread);
		EDGELIST::iterator callIt = callingNodes.begin();
		for (; callIt != callingNodes.end(); ++callIt)
			if (callIt->first == lastNode) break;
		assert(callIt != callingNodes.end());
		return make_pair(callIt->second, callIt->second);
	}

	NODEINDEX first = blk->front()->threadvertIdx.at(thread);
	NODEINDEX last = blk->back()->threadvertIdx.at(thread);
	return make_pair(first, last);
}

void diff_plotter::render() 
{
	EDGELIST::iterator edgeSeqItG1;
	EDGELIST::iterator edgeSeqEndG1;
	
	proto_graph *g1Proto = graph1->get_protoGraph();
	proto_graph *g2Proto = graph2->get_protoGraph();

	obtainMutex(g1Proto->animationListsMutex, 9932);
	obtainMutex(g2Proto->animationListsMutex, 9932);
	g1ProcessData = clientState->glob_piddata_map.at(g1Proto->get_piddata()->PID);
	g2ProcessData = clientState->glob_piddata_map.at(g2Proto->get_piddata()->PID);
	vector <ANIMATIONENTRY> *graph1Data = g1Proto->getSavedAnimData();
	vector <ANIMATIONENTRY> *graph2Data = g2Proto->getSavedAnimData();

	unsigned long renderIdx = 0;
	unsigned long renderEnd = graph1Data->size();

	PID_TID g1TID = g1Proto->get_TID();
	PID_TID g2TID = g2Proto->get_TID();
	ANIMATIONENTRY *g1Entry, *g2Entry;

	bool die = false;
	
	GRAPH_DISPLAY_DATA *linedata = diffgraph->get_mainlines();
	NODEPAIR first_lastNodeG1, first_lastNodeG2;

	matchingEdgeColour = al_col_green;
	divergingEdgeColour = al_col_red;
	divergingEdgeColour.a *= 0.3;

	edgeColour = matchingEdgeColour;

	map <NODEPAIR, bool> renderedEdges;

	for (; renderIdx < renderEnd; ++renderIdx)
	{
		//stop comparing once divergence is found
		//just draw the rest of the larger trace in red
		g1Entry = &graph1Data->at(renderIdx);

		if (renderIdx < graph2Data->size())
			g2Entry = &graph2Data->at(renderIdx);
		else
		{
			if (!divergenceFound)
				mark_divergence();
			g2Entry = g1Entry;
			g2ProcessData = g1ProcessData;
			g2TID = g1TID;
		}

		if ((g1Entry->entryType != g2Entry->entryType) && !divergenceFound)
			mark_divergence();	

		NODEPAIR nextEdge;
		switch (g1Entry->entryType)
		{
			case ANIM_LOOP_LAST:
			case ANIM_UNCHAINED:
				continue;

			case ANIM_UNCHAINED_RESULTS:
				if ((g1Entry->count != g2Entry->count) && !divergenceFound)
				{
					cout << "[rgat]Divergence detected: Graph 1 (TID" << g1TID << ") returned from unchained with " << g1Entry->count 
						<< " iterations at 0x" << g1Entry->blockAddr << " wheras graph2 had " << g2Entry->count << " iterations" << endl;
					mark_divergence();
				}
				continue;

			case ANIM_UNCHAINED_DONE:
			{
				first_lastNodeG1 = firstLastNode(g1Entry->blockAddr, g1Entry->blockID, g1ProcessData, g1TID); 
				lastNode = first_lastNodeG1.second;

				if (!divergenceFound)
				{
					first_lastNodeG2 = firstLastNode(g2Entry->blockAddr, g2Entry->blockID, g2ProcessData, g2TID);
					if (first_lastNodeG1.first != first_lastNodeG2.first)
					{
						cout << "[rgat]Divergence detected: Graph 1 (TID" << g1TID << ") left unchained area to node " << first_lastNodeG1.first << " (0x"
							<< g1Entry->blockAddr << " wheras Graph 2 left to node " << first_lastNodeG2.first << " (0x" << g2Entry->blockAddr << ")" << endl;
						mark_divergence();
					}
				}
				continue;
			}

			case ANIM_LOOP:
			{
				first_lastNodeG1 = firstLastNode(g1Entry->blockAddr, g1Entry->blockID, g1ProcessData, g1TID);
				if (!divergenceFound)
				{
					first_lastNodeG2 = firstLastNode(g2Entry->blockAddr, g2Entry->blockID, g2ProcessData, g2TID);
					if (first_lastNodeG1.first != first_lastNodeG2.first)
					{
						cout << "[rgat]Divergence detected: Graph 1 (TID" << g1TID << ") loop hit node " << first_lastNodeG1.first << " (0x"
							<< g1Entry->blockAddr << ") while Graph 2 hit node " << first_lastNodeG2.first << " (0x" << g2Entry->blockAddr << endl;
						mark_divergence();
						break;
					}

					if (g1Entry->count != g2Entry->count)
					{
						cout << "[rgat]Divergence detected: Graph 1 (TID" << g1TID << ") entered " << g1Entry->count << "iteration loop at (0x"
							<< g1Entry->blockAddr << " ) while Graph 2 entered " << g2Entry->count << " iteration loop" << endl;
						mark_divergence();
						break;
					}
				}
				break;
			}


			case ANIM_EXEC_EXCEPTION:
			{
				
				INSLIST *faultingBlockG1 = getDisassemblyBlock(g1Entry->blockAddr, g1Entry->blockID, g1ProcessData, &die);
				first_lastNodeG1.first = faultingBlockG1->front()->threadvertIdx.at(g1TID);
				first_lastNodeG1.second = faultingBlockG1->at(g1Entry->count)->threadvertIdx.at(g1TID);

				if (!divergenceFound)
				{
					INSLIST *faultingBlockG2 = getDisassemblyBlock(g2Entry->blockAddr, g2Entry->blockID, g2ProcessData, &die);
					first_lastNodeG2.first = faultingBlockG2->front()->threadvertIdx.at(g2TID);
					
					if (first_lastNodeG1.first != first_lastNodeG2.first)
					{
						cout << "[rgat]Divergence detected: Graph 1 (TID" << g1TID << ") executed node " << first_lastNodeG1.first << " (0x"
							<< g1Entry->blockAddr << ") while Graph 2 executed node " << first_lastNodeG2.first << " (0x" << g2Entry->blockAddr << ")" << endl;
						mark_divergence();
						break;
					}

					if (g1Entry->count != g2Entry->count)
					{
						cout << "[rgat]Divergence detected: Graph 1 (TID" << g1TID << ") exeption in block " << first_lastNodeG1.first << " (0x"
							<< g1Entry->blockAddr << ") after " << g1Entry->count << " instructions while Graph 2 executed " << g2Entry->count <<
							" instructions" << endl;
						mark_divergence();
						break;
					}
				}
				break;
			}

			default:
			{
				
				first_lastNodeG1 = firstLastNode(g1Entry->blockAddr, g1Entry->blockID, g1ProcessData, g1TID);

				if (!divergenceFound)
				{				
					first_lastNodeG2 = firstLastNode(g2Entry->blockAddr, g2Entry->blockID, g2ProcessData, g2TID);
					if (first_lastNodeG1.first != first_lastNodeG2.first)
					{
						cout << "[rgat]Divergence detected: Different blocks. Graph 1 (TID" << g1TID << ") executed node " << first_lastNodeG1.first << " (0x"
							<< g1Entry->blockAddr << ") while Graph 2 executed node " << first_lastNodeG2.first << " (0x" << g2Entry->blockAddr << ")" << endl;
						mark_divergence();
					}
				}
				break;
			}
		} 

		//render edge between previous block and this block
		if (renderIdx > 0)
		{
			nextEdge = make_pair(lastNode, first_lastNodeG1.first);
			if (g1Proto->edge_exists(nextEdge, 0))
			{

				map <NODEPAIR, bool>::iterator edgeIt = renderedEdges.find(nextEdge);
				if (edgeIt == renderedEdges.end())
				{
					graph1->render_edge(nextEdge, linedata, &edgeColour, false, true);
					renderedEdges[nextEdge] = true;
				}
			}
		}

		//draw block internal edges
		for (unsigned int i = first_lastNodeG1.first; i < first_lastNodeG1.second; ++i)
		{
			NODEPAIR edge = make_pair(i, i + 1);
			map <NODEPAIR, bool>::iterator edgeIt = renderedEdges.find(edge);
			if (edgeIt == renderedEdges.end())
			{
				graph1->render_edge(edge, linedata, &edgeColour, false, true);
				renderedEdges[edge] = true;
			}
		}

		

		lastNode = first_lastNodeG1.second;
	}

	dropMutex(g1Proto->animationListsMutex);
	dropMutex(g2Proto->animationListsMutex);
	diffgraph->needVBOReload_main = true;

	diffgraph->set_diffgraph_nodes(graph1->get_diffgraph_nodes());
}