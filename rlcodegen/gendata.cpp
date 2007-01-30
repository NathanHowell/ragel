/*
 *  Copyright 2005-2006 Adrian Thurston <thurston@cs.queensu.ca>
 */

/*  This file is part of Ragel.
 *
 *  Ragel is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  Ragel is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with Ragel; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include "gendata.h"

/* Code Generators. */
#include "gvdotgen.h"

#include <iostream>

using std::cerr;
using std::endl;

CodeGenData *cgd = 0;

void CodeGenData::createMachine()
{
	redFsm = new RedFsmAp();
}

void CodeGenData::initActionList( unsigned long length )
{ 
	allActions = new Action[length];
	for ( unsigned long a = 0; a < length; a++ )
		actionList.append( allActions+a );
}

void CodeGenData::newAction( int anum, char *name, int line, 
		int col, InlineList *inlineList )
{
	allActions[anum].actionId = anum;
	allActions[anum].name = name;
	allActions[anum].loc.line = line;
	allActions[anum].loc.col = col;
	allActions[anum].inlineList = inlineList;
}

void CodeGenData::initActionTableList( unsigned long length )
{ 
	allActionTables = new RedAction[length];
}

void CodeGenData::initStateList( unsigned long length )
{
	allStates = new RedStateAp[length];
	for ( unsigned long s = 0; s < length; s++ )
		redFsm->stateList.append( allStates+s );
}

void CodeGenData::setStartState( unsigned long startState )
{
	this->startState = startState;
}

void CodeGenData::addEntryPoint( char *name, unsigned long entryState )
{
	entryPointIds.append( entryState );
	entryPointNames.append( name );
}

void CodeGenData::initTransList( int snum, unsigned long length )
{
	/* Could preallocate the out range to save time growing it. For now do
	 * nothing. */
}

void CodeGenData::newTrans( int snum, int tnum, Key lowKey, 
		Key highKey, long targ, long action )
{
	/* Get the current state and range. */
	RedStateAp *curState = allStates + snum;
	RedTransList &destRange = curState->outRange;

	/* Make the new transitions. */
	RedStateAp *targState = targ >= 0 ? (allStates + targ) : 
			wantComplete ? redFsm->getErrorState() : 0;
	RedAction *actionTable = action >= 0 ? (allActionTables + action) : 0;
	RedTransAp *trans = redFsm->allocateTrans( targState, actionTable );
	RedTransEl transEl( lowKey, highKey, trans );

	if ( wantComplete ) {
		/* If the machine is to be complete then we need to fill any gaps with
		 * the error transitions. */
		if ( destRange.length() == 0 ) {
			/* Range is currently empty. */
			if ( keyOps->minKey < lowKey ) {
				/* The first range doesn't start at the low end. */
				Key fillHighKey = lowKey;
				fillHighKey.decrement();

				/* Create the filler with the state's error transition. */
				RedTransEl newTel( keyOps->minKey, fillHighKey, redFsm->getErrorTrans() );
				destRange.append( newTel );
			}
		}
		else {
			/* The range list is not empty, get the the last range. */
			RedTransEl *last = &destRange[destRange.length()-1];
			Key nextKey = last->highKey;
			nextKey.increment();
			if ( nextKey < lowKey ) {
				/* There is a gap to fill. Make the high key. */
				Key fillHighKey = lowKey;
				fillHighKey.decrement();

				/* Create the filler with the state's error transtion. */
				RedTransEl newTel( nextKey, fillHighKey, redFsm->getErrorTrans() );
				destRange.append( newTel );
			}
		}
	}

	/* Filler taken care of. Append the range. */
	destRange.append( RedTransEl( lowKey, highKey, trans ) );
}

void CodeGenData::finishTransList( int snum )
{
	/* Get the current state and range. */
	RedStateAp *curState = allStates + snum;
	RedTransList &destRange = curState->outRange;

	/* If building a complete machine we may need filler on the end. */
	if ( wantComplete ) {
		/* Check if there are any ranges already. */
		if ( destRange.length() == 0 ) {
			/* Fill with the whole alphabet. */
			/* Add the range on the lower and upper bound. */
			RedTransEl newTel( keyOps->minKey, keyOps->maxKey, redFsm->getErrorTrans() );
			destRange.append( newTel );
		}
		else {
			/* Get the last and check for a gap on the end. */
			RedTransEl *last = &destRange[destRange.length()-1];
			if ( last->highKey < keyOps->maxKey ) {
				/* Make the high key. */
				Key fillLowKey = last->highKey;
				fillLowKey.increment();

				/* Create the new range with the error trans and append it. */
				RedTransEl newTel( fillLowKey, keyOps->maxKey, redFsm->getErrorTrans() );
				destRange.append( newTel );
			}
		}
	}
}

void CodeGenData::setFinal( int snum )
{
	RedStateAp *curState = allStates + snum;
	curState->isFinal = true;
}


void CodeGenData::setStateActions( int snum, long toStateAction, 
			long fromStateAction, long eofAction )
{
	RedStateAp *curState = allStates + snum;
	if ( toStateAction >= 0 )
		curState->toStateAction = allActionTables + toStateAction;
	if ( fromStateAction >= 0 )
		curState->fromStateAction = allActionTables + fromStateAction;
	if ( eofAction >= 0 )
		curState->eofAction = allActionTables + eofAction;
}

void CodeGenData::resolveTargetStates( InlineList *inlineList )
{
	for ( InlineList::Iter item = *inlineList; item.lte(); item++ ) {
		switch ( item->type ) {
		case InlineItem::Goto: case InlineItem::Call:
		case InlineItem::Next: case InlineItem::Entry:
			item->targState = allStates + item->targId;
			break;
		default:
			break;
		}

		if ( item->children != 0 )
			resolveTargetStates( item->children );
	}
}


void CodeGenData::finishMachine()
{
	if ( redFsm->forcedErrorState )
		redFsm->getErrorState();

	/* We get the start state as an offset, set the pointer now. */
	redFsm->startState = allStates + startState;
	for ( EntryIdVect::Iter en = entryPointIds; en.lte(); en++ )
		redFsm->entryPoints.insert( allStates + *en );

	for ( ActionList::Iter a = actionList; a.lte(); a++ )
		resolveTargetStates( a->inlineList );

	/* Note that even if we want a complete graph we do not give the error
	 * state a default transition. All machines break out of the processing
	 * loop when in the error state. */

	if ( codeStyle == GenGoto || codeStyle == GenFGoto || codeStyle == GenIpGoto ) {
		for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
			for ( StateCondList::Iter sci = st->stateCondList; sci.lte(); sci++ )
				st->stateCondVect.append( sci );
		}
	}
}


bool CodeGenData::setAlphType( char *data )
{
	/* FIXME: This should validate the alphabet type selection. */
	HostType *alphType = hostLang->hostTypes + atoi(data);
	thisKeyOps.setAlphType( alphType );
	return true;
}

void CodeGenData::initCondSpaceList( ulong length )
{
	allCondSpaces = new CondSpace[length];
	for ( ulong c = 0; c < length; c++ )
		condSpaceList.append( allCondSpaces + c );
}

void CodeGenData::newCondSpace( int cnum, int condSpaceId, Key baseKey )
{
	CondSpace *cond = allCondSpaces + cnum;
	cond->condSpaceId = condSpaceId;
	cond->baseKey = baseKey;
}

void CodeGenData::condSpaceItem( int cnum, long condActionId )
{
	CondSpace *cond = allCondSpaces + cnum;
	cond->condSet.append( allActions + condActionId );
}

void CodeGenData::initStateCondList( int snum, ulong length )
{
	/* Could preallocate these, as we could with transitions. */
}

void CodeGenData::addStateCond( int snum, Key lowKey, Key highKey, long condNum )
{
	RedStateAp *curState = allStates + snum;

	/* Create the new state condition. */
	StateCond *stateCond = new StateCond;
	stateCond->lowKey = lowKey;
	stateCond->highKey = highKey;

	/* Assign it a cond space. */
	CondSpace *condSpace = allCondSpaces + condNum;
	stateCond->condSpace = condSpace;

	curState->stateCondList.append( stateCond );
}


CondSpace *CodeGenData::findCondSpace( Key lowKey, Key highKey )
{
	for ( CondSpaceList::Iter cs = condSpaceList; cs.lte(); cs++ ) {
		Key csHighKey = cs->baseKey;
		csHighKey += keyOps->alphSize() * (1 << cs->condSet.length());

		if ( lowKey >= cs->baseKey && highKey <= csHighKey )
			return cs;
	}
	return 0;
}

Condition *CodeGenData::findCondition( Key key )
{
	for ( ConditionList::Iter cond = conditionList; cond.lte(); cond++ ) {
		Key upperKey = cond->baseKey + (1 << cond->condSet.length());
		if ( cond->baseKey <= key && key <= upperKey )
			return cond;
	}
	return 0;
}

Key CodeGenData::findMaxKey()
{
	Key maxKey = keyOps->maxKey;
	for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
		assert( st->outSingle.length() == 0 );
		assert( st->defTrans == 0 );

		long rangeLen = st->outRange.length();
		if ( rangeLen > 0 ) {
			Key highKey = st->outRange[rangeLen-1].highKey;
			if ( highKey > maxKey )
				maxKey = highKey;
		}
	}
	return maxKey;
}

void CodeGenData::findFinalActionRefs()
{
	for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
		/* Rerence count out of single transitions. */
		for ( RedTransList::Iter rtel = st->outSingle; rtel.lte(); rtel++ ) {
			if ( rtel->value->action != 0 ) {
				rtel->value->action->numTransRefs += 1;
				for ( ActionTable::Iter item = rtel->value->action->key; item.lte(); item++ )
					item->value->numTransRefs += 1;
			}
		}

		/* Reference count out of range transitions. */
		for ( RedTransList::Iter rtel = st->outRange; rtel.lte(); rtel++ ) {
			if ( rtel->value->action != 0 ) {
				rtel->value->action->numTransRefs += 1;
				for ( ActionTable::Iter item = rtel->value->action->key; item.lte(); item++ )
					item->value->numTransRefs += 1;
			}
		}

		/* Reference count default transition. */
		if ( st->defTrans != 0 && st->defTrans->action != 0 ) {
			st->defTrans->action->numTransRefs += 1;
			for ( ActionTable::Iter item = st->defTrans->action->key; item.lte(); item++ )
				item->value->numTransRefs += 1;
		}

		/* Reference count to state actions. */
		if ( st->toStateAction != 0 ) {
			st->toStateAction->numToStateRefs += 1;
			for ( ActionTable::Iter item = st->toStateAction->key; item.lte(); item++ )
				item->value->numToStateRefs += 1;
		}

		/* Reference count from state actions. */
		if ( st->fromStateAction != 0 ) {
			st->fromStateAction->numFromStateRefs += 1;
			for ( ActionTable::Iter item = st->fromStateAction->key; item.lte(); item++ )
				item->value->numFromStateRefs += 1;
		}

		/* Reference count EOF actions. */
		if ( st->eofAction != 0 ) {
			st->eofAction->numEofRefs += 1;
			for ( ActionTable::Iter item = st->eofAction->key; item.lte(); item++ )
				item->value->numEofRefs += 1;
		}
	}
}

void CodeGenData::analyzeAction( Action *act, InlineList *inlineList )
{
	for ( InlineList::Iter item = *inlineList; item.lte(); item++ ) {
		/* Only consider actions that are referenced. */
		if ( act->numRefs() > 0 ) {
			if ( item->type == InlineItem::Goto || item->type == InlineItem::GotoExpr )
				redFsm->bAnyActionGotos = true;
			else if ( item->type == InlineItem::Call || item->type == InlineItem::CallExpr )
				redFsm->bAnyActionCalls = true;
			else if ( item->type == InlineItem::Ret )
				redFsm->bAnyActionRets = true;
		}

		/* Check for various things in regular actions. */
		if ( act->numTransRefs > 0 || act->numToStateRefs > 0 || act->numFromStateRefs > 0 ) {
			/* Any returns in regular actions? */
			if ( item->type == InlineItem::Ret )
				redFsm->bAnyRegActionRets = true;

			/* Any next statements in the regular actions? */
			if ( item->type == InlineItem::Next || item->type == InlineItem::NextExpr )
				redFsm->bAnyRegNextStmt = true;

			/* Any by value control in regular actions? */
			if ( item->type == InlineItem::CallExpr || item->type == InlineItem::GotoExpr )
				redFsm->bAnyRegActionByValControl = true;

			/* Any references to the current state in regular actions? */
			if ( item->type == InlineItem::Curs )
				redFsm->bAnyRegCurStateRef = true;

			if ( item->type == InlineItem::Break )
				redFsm->bAnyRegBreak = true;

			if ( item->type == InlineItem::LmSwitch && item->handlesError )
				redFsm->bAnyLmSwitchError = true;
		}

		if ( item->children != 0 )
			analyzeAction( act, item->children );
	}
}

void CodeGenData::analyzeActionList( RedAction *redAct, InlineList *inlineList )
{
	for ( InlineList::Iter item = *inlineList; item.lte(); item++ ) {
		/* Any next statements in the action table? */
		if ( item->type == InlineItem::Next || item->type == InlineItem::NextExpr )
			redAct->bAnyNextStmt = true;

		/* Any references to the current state. */
		if ( item->type == InlineItem::Curs )
			redAct->bAnyCurStateRef = true;

		if ( item->type == InlineItem::Break )
			redAct->bAnyBreakStmt = true;

		if ( item->children != 0 )
			analyzeActionList( redAct, item->children );
	}
}

/* Assign ids to referenced actions. */
void CodeGenData::assignActionIds()
{
	int nextActionId = 0;
	for ( ActionList::Iter act = cgd->actionList; act.lte(); act++ ) {
		/* Only ever interested in referenced actions. */
		if ( act->numRefs() > 0 )
			act->actionId = nextActionId++;
	}
}

void CodeGenData::setValueLimits()
{
	redFsm->maxSingleLen = 0;
	redFsm->maxRangeLen = 0;
	redFsm->maxKeyOffset = 0;
	redFsm->maxIndexOffset = 0;
	redFsm->maxActListId = 0;
	redFsm->maxActionLoc = 0;
	redFsm->maxActArrItem = 0;
	redFsm->maxSpan = 0;
	redFsm->maxCondSpan = 0;
	redFsm->maxFlatIndexOffset = 0;
	redFsm->maxCondOffset = 0;
	redFsm->maxCondLen = 0;
	redFsm->maxCondSpaceId = 0;
	redFsm->maxCondIndexOffset = 0;

	/* In both of these cases the 0 index is reserved for no value, so the max
	 * is one more than it would be if they started at 0. */
	redFsm->maxIndex = redFsm->transSet.length();
	redFsm->maxCond = cgd->condSpaceList.length(); 

	/* The nextStateId - 1 is the last state id assigned. */
	redFsm->maxState = redFsm->nextStateId - 1;

	for ( CondSpaceList::Iter csi = cgd->condSpaceList; csi.lte(); csi++ ) {
		if ( csi->condSpaceId > redFsm->maxCondSpaceId )
			redFsm->maxCondSpaceId = csi->condSpaceId;
	}

	for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
		/* Maximum cond length. */
		if ( st->stateCondList.length() > redFsm->maxCondLen )
			redFsm->maxCondLen = st->stateCondList.length();

		/* Maximum single length. */
		if ( st->outSingle.length() > redFsm->maxSingleLen )
			redFsm->maxSingleLen = st->outSingle.length();

		/* Maximum range length. */
		if ( st->outRange.length() > redFsm->maxRangeLen )
			redFsm->maxRangeLen = st->outRange.length();

		/* The key offset index offset for the state after last is not used, skip it.. */
		if ( ! st.last() ) {
			redFsm->maxCondOffset += st->stateCondList.length();
			redFsm->maxKeyOffset += st->outSingle.length() + st->outRange.length()*2;
			redFsm->maxIndexOffset += st->outSingle.length() + st->outRange.length() + 1;
		}

		/* Max cond span. */
		if ( st->condList != 0 ) {
			unsigned long long span = keyOps->span( st->condLowKey, st->condHighKey );
			if ( span > redFsm->maxCondSpan )
				redFsm->maxCondSpan = span;
		}

		/* Max key span. */
		if ( st->transList != 0 ) {
			unsigned long long span = keyOps->span( st->lowKey, st->highKey );
			if ( span > redFsm->maxSpan )
				redFsm->maxSpan = span;
		}

		/* Max cond index offset. */
		if ( ! st.last() ) {
			if ( st->condList != 0 )
				redFsm->maxCondIndexOffset += keyOps->span( st->condLowKey, st->condHighKey );
		}

		/* Max flat index offset. */
		if ( ! st.last() ) {
			if ( st->transList != 0 )
				redFsm->maxFlatIndexOffset += keyOps->span( st->lowKey, st->highKey );
			redFsm->maxFlatIndexOffset += 1;
		}
	}

	for ( ActionTableMap::Iter at = redFsm->actionMap; at.lte(); at++ ) {
		/* Maximum id of action lists. */
		if ( at->actListId+1 > redFsm->maxActListId )
			redFsm->maxActListId = at->actListId+1;

		/* Maximum location of items in action array. */
		if ( at->location+1 > redFsm->maxActionLoc )
			redFsm->maxActionLoc = at->location+1;

		/* Maximum values going into the action array. */
		if ( at->key.length() > redFsm->maxActArrItem )
			redFsm->maxActArrItem = at->key.length();
		for ( ActionTable::Iter item = at->key; item.lte(); item++ ) {
			if ( item->value->actionId > redFsm->maxActArrItem )
				redFsm->maxActArrItem = item->value->actionId;
		}
	}
}



/* Gather various info on the machine. */
void CodeGenData::analyzeMachine()
{
	/* Find the true count of action references.  */
	findFinalActionRefs();

	/* Check if there are any calls in action code. */
	for ( ActionList::Iter act = cgd->actionList; act.lte(); act++ ) {
		/* Record the occurrence of various kinds of actions. */
		if ( act->numToStateRefs > 0 )
			redFsm->bAnyToStateActions = true;
		if ( act->numFromStateRefs > 0 )
			redFsm->bAnyFromStateActions = true;
		if ( act->numEofRefs > 0 )
			redFsm->bAnyEofActions = true;
		if ( act->numTransRefs > 0 )
			redFsm->bAnyRegActions = true;

		/* Recurse through the action's parse tree looking for various things. */
		analyzeAction( act, act->inlineList );
	}

	/* Analyze reduced action lists. */
	for ( ActionTableMap::Iter redAct = redFsm->actionMap; redAct.lte(); redAct++ ) {
		for ( ActionTable::Iter act = redAct->key; act.lte(); act++ )
			analyzeActionList( redAct, act->value->inlineList );
	}

	/* Find states that have transitions with actions that have next
	 * statements. */
	for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
		/* Check any actions out of outSinge. */
		for ( RedTransList::Iter rtel = st->outSingle; rtel.lte(); rtel++ ) {
			if ( rtel->value->action != 0 && rtel->value->action->anyCurStateRef() )
				st->bAnyRegCurStateRef = true;
		}

		/* Check any actions out of outRange. */
		for ( RedTransList::Iter rtel = st->outRange; rtel.lte(); rtel++ ) {
			if ( rtel->value->action != 0 && rtel->value->action->anyCurStateRef() )
				st->bAnyRegCurStateRef = true;
		}

		/* Check any action out of default. */
		if ( st->defTrans != 0 && st->defTrans->action != 0 && 
				st->defTrans->action->anyCurStateRef() )
			st->bAnyRegCurStateRef = true;
		
		if ( st->stateCondList.length() > 0 )
			redFsm->bAnyConditions = true;
	}

	/* Assign ids to actions that are referenced. */
	assignActionIds();

	/* Set the maximums of various values used for deciding types. */
	setValueLimits();
}

/* Generate the code for an fsm. Assumes parseData is set up properly. Called
 * by parser code. */
void CodeGenData::prepareMachine()
{
	if ( hasBeenPrepared )
		return;
	hasBeenPrepared = true;
	
	/* Do this before distributing transitions out to singles and defaults
	 * makes life easier. */
	redFsm->maxKey = findMaxKey();

	redFsm->assignActionLocs();

	/* Order the states. */
	redFsm->depthFirstOrdering();

	if ( codeStyle == GenGoto || codeStyle == GenFGoto || 
			codeStyle == GenIpGoto || codeStyle == GenSplit )
	{
		/* For goto driven machines we can keep the original depth
		 * first ordering because it's ok if the state ids are not
		 * sequential. Split the the ids by final state status. */
		redFsm->sortStateIdsByFinal();
	}
	else {
		/* For table driven machines the location of the state is used to
		 * identify it so the states must be sorted by their final ids.
		 * Though having a deterministic ordering is important,
		 * specifically preserving the depth first ordering is not because
		 * states are stored in tables. */
		redFsm->sortStatesByFinal();
		redFsm->sequentialStateIds();
	}

	/* Find the first final state. This is the final state with the lowest
	 * id.  */
	redFsm->findFirstFinState();

	/* Choose default transitions and the single transition. */
	redFsm->chooseDefaultSpan();
		
	/* Maybe do flat expand, otherwise choose single. */
	if ( codeStyle == GenFlat || codeStyle == GenFFlat )
		redFsm->makeFlat();
	else
		redFsm->chooseSingle();

	/* If any errors have occured in the input file then don't write anything. */
	if ( gblErrorCount > 0 )
		return;
	
	if ( codeStyle == GenSplit )
		redFsm->partitionFsm( numSplitPartitions );

	if ( codeStyle == GenIpGoto || codeStyle == GenSplit )
		redFsm->setInTrans();

	/* Anlayze Machine will find the final action reference counts, among
	 * other things. We will use these in reporting the usage
	 * of fsm directives in action code. */
	analyzeMachine();

	/* Make a code generator that will output the header/code. */
	if ( codeGen == 0 )
		codeGen = makeCodeGen( this );
	codeGen->redFsm = redFsm;

	/* Determine if we should use indicies. */
	codeGen->calcIndexSize();
}

void CodeGenData::generateGraphviz()
{
	/* Do ordering and choose state ids. */
	redFsm->depthFirstOrdering();
	redFsm->sequentialStateIds();

	/* For dot file generation we want to pick default transitions. */
	redFsm->chooseDefaultSpan();

	/* Make the generator. */
	GraphvizDotGen dotGen( fsmName, this, redFsm, out );

	/* Write out with it. */
	dotGen.writeDotFile();
}

void CodeGenData::generateCode()
{
	if ( writeOps & WO_NOEND )
		hasEnd = false;

	if ( writeOps & WO_NOERROR )
		writeErr = false;

	if ( writeOps & WO_NOPREFIX )
		dataPrefix = false;
	
	if ( writeOps & WO_NOFF )
		writeFirstFinal = false;

	if ( writeData || writeInit || writeExec || writeEOF ) {
		prepareMachine();

		/* Force a newline. */
		out << "\n";
		genLineDirective( out );
	}
	

	if ( writeExec ) {
		/* Must set labels immediately before writing because we may depend
		 * on the noend write option. */
		codeGen->setLabelsNeeded();
	}

	if ( writeData )
		codeGen->writeOutData();
	
	if ( writeInit )
		codeGen->writeOutInit();

	if ( writeExec )
		codeGen->writeOutExec();

	if ( writeEOF )
		codeGen->writeOutEOF();
}

void CodeGenData::generate()
{
	if ( redFsm != 0 ) {
		if ( outputFormat == OutCode )
			generateCode();
		else if ( outputFormat == OutGraphvizDot && !graphvizDone ) {
			graphvizDone = true;
			generateGraphviz();
		}
	}
}

void lineDirective( ostream &out, char *fileName, int line )
{
	if ( hostLangType != JavaCode ) {
		/* Write the preprocessor line info for to the input file. */
		out << "#line " << line  << " \"";
		for ( char *pc = fileName; *pc != 0; pc++ ) {
			if ( *pc == '\\' )
				out << "\\\\";
			else
				out << *pc;
		}
		out << "\"\n";
	}
}

void genLineDirective( ostream &out )
{
	assert( outputFormat == OutCode );
	std::streambuf *sbuf = out.rdbuf();
	output_filter *filter = static_cast<output_filter*>(sbuf);
	lineDirective( out, filter->fileName, filter->line + 1 );
}
