/*

Assign addresses to variables

(c) 2010 Rudolf Kudla 
Licensed under the MIT license: http://www.opensource.org/licenses/mit-license.php

*/

#include "language.h"

//var_heap:0..255 or 32000..65000

GLOBAL MemHeap VAR_HEAP;		// variable heap (or zero page heap), this is heap from which variables are preferably allocated

void ProcClearProcessed(Var * proc)
{
	SetFlagOff(proc->flags, VarProcessed);
}

Bool ProcCallsProc(Var * proc, Var * called_proc)
/*
Purpose:
	Test, whether procedure 'called_proc' is called (even indirectly) from specified procedure.
*/{
	Bool calls = false;
	Instr * i;
	InstrBlock * blk;

	// We use VarProcessed to prevent resursion for recursive functions, even
	// if recursion is not supported yet.

	if (FlagOff(proc->flags, VarProcessed)) {
		SetFlagOn(proc->flags, VarProcessed);
		for(blk = proc->instr; blk != NULL; blk = blk->next) {
			for(i = blk->first; i != NULL; i = i->next) {
				if (i->op == INSTR_CALL) {
					if (i->result == called_proc || ProcCallsProc(i->result, called_proc)) {
						calls = true;
						goto done;
					}
				}
			}
		}
done:
		SetFlagOff(proc->flags, VarProcessed);
	}
	return calls;
}

void AllocateVariablesFromHeap(Var * scope, MemHeap * heap)
{
	Var * var;
	UInt32 size, adr;

	for (var = VarFirstLocal(scope); var != NULL; var = VarNextLocal(scope, var)) {

		// Scope can contain variables in subscope, we need to allocate them too
		if (var->mode == MODE_SCOPE) {
			AllocateVariablesFromHeap(var, heap);
		} else {
			if ((var->write > 0 || var->read > 0) && !VarIsLabel(var)) {
				if (var->adr == NULL && (var->mode == MODE_VAR || var->mode == MODE_ARG)) {
					size = TypeSize(var->type);		
					if (size > 0) {
						if (HeapAllocBlock(heap, size, &adr) || HeapAllocBlock(&VAR_HEAP, size, &adr)) {
							var->adr = VarNewInt(adr);
						} else {
							// failed to alloc in zero page
						}
					}
				}
			}
		}
	}
}

void HeapAddVariables(MemHeap * heap, Var * scope)
{
	Var * var, * vadr;
	UInt32 size, adr;

	for (var = VarFirstLocal(scope); var != NULL; var = VarNextLocal(scope, var)) {
		if (var->mode == MODE_SCOPE) {
			HeapAddVariables(heap, var);
		} else {
			if (var->mode == MODE_VAR || var->mode == MODE_ARG) {
				size = TypeSize(var->type);
				vadr = var->adr;
				if (size > 0 && vadr != NULL) {
					if (vadr->type->variant == TYPE_INT) {
						adr  = vadr->n;
						HeapAddBlock(heap, adr, size);
					}
				}
			}
		}
	}
}

void HeapRemoveVariables(MemHeap * heap, Var * scope)
{
	Var * var, * vadr;
	UInt32 size, adr;

	for (var = VarFirstLocal(scope); var != NULL; var = VarNextLocal(scope, var)) {
		if (var->mode == MODE_SCOPE) {
			HeapRemoveVariables(heap, var);
		} else {
			if (var->mode == MODE_VAR || var->mode == MODE_ARG) {
				size = TypeSize(var->type);
				vadr = var->adr;
				if (size > 0 && vadr != NULL) {
					if (vadr->type->variant == TYPE_INT) {
						adr  = vadr->n;
						HeapRemoveBlock(heap, adr, size);
					}
				}
			}
		}
	}
}

extern Var   ROOT_PROC;

void AllocateVariables(Var * proc)
{
	Var * proc2;
	Type * type;
	MemHeap heap;
	
	HeapInit(&heap);

	//==== Find space already allocated for other procedure variables that can be reused
	//     We can reuse variables from procedures, that this procedure 
	//     does not call (even indirectly) and which does not call this procedure.

	for(proc2 = VarFirst(); proc2 != NULL; proc2 = VarNext(proc2)) {
		type = proc2->type;
		if (type != NULL && type->variant == TYPE_PROC && proc2->read > 0 && proc2->instr != NULL) {
//			printf("%s -> %s", proc2->name, proc->name);
			if (proc2 != proc && !ProcCallsProc(proc, proc2) && !ProcCallsProc(proc2, proc)) {
//				if (/*StrEqual(proc->name, "copyblock") &&*/ StrEqual(proc->name, "drawmainscreen")) {
//					printf("***\n");
//				}
				HeapAddVariables(&heap, proc2);
//				if (StrEqual(proc->name, "drawmainscreen")) {
//					HeapPrint(&heap);
//				}

			} else {
//				printf("... Dependent");
			}
//			printf("\n");
		}
	}

	//==== Remove space allocated by procedures we are calling or which call us

	//TODO: Procedure must not have body (may be external) and still define arguments

	for(proc2 = VarFirst(); proc2 != NULL; proc2 = VarNext(proc2)) {
		type = proc2->type;

//		if (StrEqual(proc2->name, "copyblock") && StrEqual(proc->name, "drawmainscreen")) {
//			printf("***");
//		}

		if (type != NULL && type->variant == TYPE_PROC && proc2->read > 0 && proc2->instr != NULL) {
			if (proc2 != proc && (FlagOn(proc2->flags, VarProcInterrupt) || ProcCallsProc(proc, proc2) || ProcCallsProc(proc2, proc))) {
//				if (StrEqual(proc->name, "drawmainscreen")) {
//					HeapPrint(&heap);
//				}
				HeapRemoveVariables(&heap, proc2);
//				if (StrEqual(proc->name, "drawmainscreen")) {
//					HeapPrint(&heap);
//				}
			}
		}
	}
	
	//==== Allocate space for all variables, that has not been assigned yet

	AllocateVariablesFromHeap(proc, &heap);

	HeapCleanup(&heap);
}