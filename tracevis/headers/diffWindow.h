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
The class for the divergence/diff selection window
*/

#pragma once
#include "GUIStructs.h"
#include "plotted_graph.h"
#include <Agui/Agui.hpp>
#include <Agui/Backends/Allegro5/Allegro5.hpp>
#include "Agui\Widgets\DropDown\DropDown.hpp"
#include "Agui\Widgets\Label\Label.hpp"
#include "Agui\Widgets\RadioButton\RadioButton.hpp"
#include "OSspecific.h"

#define DIFF_INFOLABEL_X_OFFSET 25

class RadioButtonListener : public agui::ActionListener
{
public:
	RadioButtonListener(VISSTATE *state, agui::RadioButton *s1, agui::RadioButton *s2);
	virtual void actionPerformed(const agui::ActionEvent &evt)
	{
		//called due to user selecting a graph, not clicking a radio button
		if (ignoreAction) {
			ignoreAction = false;
			return;
		}

		if (evt.getSource() == source1)
			source2->setChecked(!source1->getRadioButtonState());
		else {
			if (evt.getSource() == source2)
				source1->setChecked(!source2->getRadioButtonState());
		}
	}
	void setIgnoreFlag() { ignoreAction = true; }
private:
	VISSTATE *clientState;
	bool ignoreAction = false;
	agui::RadioButton *source1;
	agui::RadioButton *source2;
};


class DiffSelectionFrame 
{
public:
	DiffSelectionFrame(agui::Gui *widgets, VISSTATE *state, agui::Font *font);
	agui::RadioButton *firstDiffLabel;
	agui::RadioButton *secondDiffLabel;

	agui::Frame *diffFrame = NULL;
	agui::Font *diffFont;
	agui::Button *diffBtn;

	int getSelectedDiff();
	void setDiffGraph(plotted_graph *graph);
	plotted_graph *get_graph(int idx);
	RadioButtonListener *radiolisten;

private:
	agui::Label *graph1Info = 0;
	agui::Label *graph1Path = 0;
	agui::Label *graph2Info = 0;
	agui::Label *graph2Path = 0;
	PID_TID graph1pid = 0, graph1tid = 0, graph2pid = 0, graph2tid = 0;
	//plotted_graph *graph1 = 0;
	//plotted_graph *graph2 = 0;
	VISSTATE *clientState;
};

class CompareButtonListener : public agui::ActionListener
{
public:
	CompareButtonListener(VISSTATE *state, DiffSelectionFrame *diffWindowPtr) {
		clientState = state; diffWindow = diffWindowPtr;
	}
	virtual void actionPerformed(const agui::ActionEvent &evt)
	{
		if (evt.getSource()->getText() == "X")
		{
			clientState->closeFrame(diffWindow->diffFrame);
			return;
		}
		clientState->modes.diffView = eDiffSelected;
	}

private:
	VISSTATE *clientState;
	DiffSelectionFrame *diffWindow;
};
