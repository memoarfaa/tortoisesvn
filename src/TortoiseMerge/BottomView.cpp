// TortoiseMerge - a Diff/Patch program

// Copyright (C) 2006-2011 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "StdAfx.h"
#include "Resource.h"
#include "AppUtils.h"

#include "bottomview.h"

IMPLEMENT_DYNCREATE(CBottomView, CBaseView)

CBottomView::CBottomView(void)
{
    m_pwndBottom = this;
    m_pState = &m_AllState.bottom;
    m_nStatusBarID = ID_INDICATOR_BOTTOMVIEW;
}

CBottomView::~CBottomView(void)
{
}


void CBottomView::AddContextItems(CMenu& popup, DiffStates state)
{
    const UINT uFlags = GetMenuFlags( state );

    CString temp;
    temp.LoadString(IDS_VIEWCONTEXTMENU_USETHEIRBLOCK);
    popup.AppendMenu(uFlags, POPUPCOMMAND_USETHEIRBLOCK, temp);
    temp.LoadString(IDS_VIEWCONTEXTMENU_USEYOURBLOCK);
    popup.AppendMenu(uFlags, POPUPCOMMAND_USEYOURBLOCK, temp);
    temp.LoadString(IDS_VIEWCONTEXTMENU_USEYOURANDTHEIRBLOCK);
    popup.AppendMenu(uFlags, POPUPCOMMAND_USEYOURANDTHEIRBLOCK, temp);
    temp.LoadString(IDS_VIEWCONTEXTMENU_USETHEIRANDYOURBLOCK);
    popup.AppendMenu(uFlags, POPUPCOMMAND_USETHEIRANDYOURBLOCK, temp);

    CBaseView::AddContextItems(popup, state);
}


void CBottomView::CleanEmptyLines()
{
    for (int viewLine = 0; viewLine < m_pwndBottom->GetViewCount(); )
    {
        DiffStates leftState = m_pwndLeft->GetViewState(viewLine);
        DiffStates rightState = m_pwndRight->GetViewState(viewLine);
        DiffStates bottomState = m_pwndBottom->GetViewState(viewLine);
        if (IsStateEmpty(leftState) && IsStateEmpty(rightState) && IsStateEmpty(bottomState))
        {
            m_pwndLeft->RemoveViewData(viewLine);
            m_pwndRight->RemoveViewData(viewLine);
            m_pwndBottom->RemoveViewData(viewLine);
            if (CUndo::GetInstance().IsGrouping()) // if use group undo -> ensure back adding goes in right (reversed) order
            {
                SaveUndoStep();
            }
            continue;
        }
        viewLine++;
    }
}

void CBottomView::UseBothBlocks(CBaseView * pwndFirst, CBaseView * pwndLast)
{
    if (!HasSelection())
        return;

    CUndo::GetInstance().BeginGrouping(); // start group undo

    int viewIndexSelectionStart = m_Screen2View[m_nSelBlockStart];
    int viewIndexAfterSelection = m_Screen2View.back() + 1;
    if (m_nSelBlockEnd + 1 < int(m_Screen2View.size()))
        viewIndexAfterSelection = m_Screen2View[m_nSelBlockEnd + 1];

    // use (copy) first block
    for (int i = m_nSelBlockStart; i <= m_nSelBlockEnd; i++)
    {
        int viewLine = m_Screen2View[i];
        viewdata lineData = pwndFirst->GetViewData(viewLine);
        lineData.ending = m_pwndBottom->lineendings;
        lineData.state = ResolveState(lineData.state);
        m_pwndBottom->SetViewData(viewLine, lineData);
        if (!IsStateEmpty(pwndFirst->GetViewState(viewLine)))
        {
            pwndFirst->SetViewState(viewLine, DIFFSTATE_YOURSADDED); // this is improper (may be DIFFSTATE_THEIRSADDED) but seems not to produce any visible bug
        }
    }
    SaveUndoStep();

    // use (insert) last block
    int viewIndex = viewIndexAfterSelection;
    for (int i = m_nSelBlockStart; i <= m_nSelBlockEnd; i++, viewIndex++)
    {
        int viewLine = m_Screen2View[i];
        viewdata lineData = pwndLast->GetViewData(viewLine);
        lineData.state = ResolveState(lineData.state);
        m_pwndBottom->InsertViewData(viewIndex, lineData);
        if (!IsStateEmpty(pwndLast->GetViewState(viewLine)))
        {
            pwndLast->SetViewState(viewLine, DIFFSTATE_THEIRSADDED); // this is improper but seems not to produce any visible bug
        }
    }
    SaveUndoStep();

    // adjust line numbers in target
    // we fix all line numbers to handle exotic cases
    UpdateViewLineNumbers();
    SaveUndoStep();

    // now insert an empty block in both first and last
    int nCount = m_nSelBlockEnd - m_nSelBlockStart + 1;
    pwndLast->InsertViewEmptyLines(viewIndexSelectionStart, nCount);
    pwndFirst->InsertViewEmptyLines(viewIndexAfterSelection, nCount);
    SaveUndoStep();

    CleanEmptyLines();
    SaveUndoStep();	

    CUndo::GetInstance().EndGrouping();

    BuildAllScreen2ViewVector();
    RecalcAllVertScrollBars();
    m_pwndBottom->SetModified();
    pwndLast->SetModified();
    pwndFirst->SetModified();
    SaveUndoStep();
    RefreshViews();
}

// note :differs to UseViewFile in EOL source, view range and using Screen2View
void CBottomView::UseViewBlock(CBaseView * pwndView)
{
    if (!HasSelection())
        return;
    
    CUndo::GetInstance().BeginGrouping(); // start group undo

    for (int i = m_nSelBlockStart; i <= m_nSelBlockEnd; i++)
    {
        int viewLine = m_Screen2View[i];
        viewdata lineData = pwndView->GetViewData(viewLine);
        lineData.ending = m_pwndBottom->lineendings;
        lineData.state = ResolveState(lineData.state);
        m_pwndBottom->SetViewData(viewLine, lineData);
    }

    CleanEmptyLines();
    SaveUndoStep();	
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    m_pwndBottom->SetModified();
    BuildAllScreen2ViewVector();
    RecalcAllVertScrollBars();
    RefreshViews();
    SaveUndoStep();
}

void CBottomView::UseViewFile(CBaseView * pwndView)
{
    CUndo::GetInstance().BeginGrouping(); // start group undo

    for (int i = 0; i < m_pwndBottom->GetViewCount(); i++)
    {
        int viewLine = i;
        viewdata lineData = pwndView->GetViewData(viewLine);
        lineData.state = ResolveState(lineData.state);
        m_pwndBottom->SetViewData(viewLine, lineData);
    }

    CleanEmptyLines();
    SaveUndoStep();	
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    m_pwndBottom->SetModified();
    BuildAllScreen2ViewVector();
    RecalcAllVertScrollBars();
    RefreshViews();
    SaveUndoStep();
}
