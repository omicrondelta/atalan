/*

Cell management

There is one global table of variables.

(c) 2010 Rudolf Kudla 
Licensed under the MIT license: http://www.opensource.org/licenses/mit-license.php

*/

#include "language.h"

GLOBAL Var * UNUSED_VARS;		     // scope in which are all the free variables
GLOBAL CellBlock * CELL_BLOCKS;       // list of all variable blocks
GLOBAL CellBlock * LAST_CELL_BLOCK;   // list of all variable blocks

//GLOBAL Var * VARS;		// global variables
//GLOBAL Var * LAST_VAR;  // Last allocated variable.

GLOBAL Var * SCOPE;		// current scope
GLOBAL UInt32 TMP_LBL_IDX;

// Used for cell enumerating
CellBlock * G_BLK;
UInt16 G_VAR_I;
extern UInt32 TMP_IDX;

void IntCellInit();


/*
We have theoretical option of supporting multiple CPUs simultaneously (this the CPUS array).
Current CPU used is stored in CPU variable.
*/

CPUType CPUS[1];			// currently, there is only one supported CPU
GLOBAL CPUType * CPU;		// current CPU (in case we use multiple CPUs in the future)

char * TMP_LBL_NAME = "_lbl";
char * SCOPE_NAME = "_s";
UInt32 SCOPE_IDX;

CellBlock * NewCellBlock()
{
	Var * var, * next;
	UInt16 i;
	CellBlock * varblk;
	varblk = MemAllocStruct(CellBlock);

	// Initialize all variables in the block as unused and put them in the list of unused variables.

	UNUSED_VARS = &varblk->vars[0];
	var = UNUSED_VARS;

	for(i=1; i < VAR_BLOCK_CAPACITY; i++) {
		next = var+1;
		var->mode = INSTR_NULL;
		var->next_in_scope = next;
		var = next;
	}
	// now initialize the last variable in the block
	var->mode = INSTR_NULL;

	return varblk;
}

Var * NewCell(InstrOp mode)
{
	Var * cell;

	if (UNUSED_VARS == NULL) {
		LAST_CELL_BLOCK->next = NewCellBlock();
		LAST_CELL_BLOCK = LAST_CELL_BLOCK->next;
	}
	cell = UNUSED_VARS;
	UNUSED_VARS = cell->next_in_scope;
	cell->next_in_scope = NULL;

	cell->mode = mode;
	cell->submode = 0;
	cell->scope = NULL;
	cell->subscope = NULL;

	return cell;
}

Var * NewCellInScope(InstrOp mode, Var * scope)
{
	Var * cell = NewCell(mode);
	CellSetScope(cell, scope);
	return cell;
}

Var * CellCopy(Var * var)
{
	Var * copy;
	copy = NewCell(var->mode);
	memcpy(copy, var, sizeof(Var));
	return copy;
}

Var * ForEachCell(Bool (*cell_fn)(Var * cell, void * data), void * data)
{
	CellBlock * blk;
	Var * cell;
	UInt16 i;
	for(blk = CELL_BLOCKS; blk != NULL; blk = blk->next) {
		for(i=0, cell = &blk->vars[0]; i < VAR_BLOCK_CAPACITY; i++, cell++) {		
			if (cell->mode != INSTR_NULL) {
				if (cell_fn(cell, data)) return cell;
			}
		}
	}
	return NULL;
}


/**************************************************************

  Scope management

  Scopes are represented as name cells without actual variable.
  However all elements in the scope are available.

***************************************************************/


void CellSetScope(Var * var, Var * scope)
{
	Var * sub;

	if (var->scope != NULL) {
		InternalError("Variable already has the scope set");
	}

	if (scope == NO_SCOPE) {
		var->scope = NULL;
		return;
	}

	if (scope == NULL) scope = SCOPE;

	var->scope = scope;

	//==== Append as last variable in scope

	if (scope->subscope == NULL) {
		scope->subscope = var;
	} else {
		// We append the variable as last variable in the scope.
		for(sub = scope->subscope; sub->next_in_scope != NULL;) {
			sub = sub->next_in_scope;
		}
		sub->next_in_scope = var;
	}


}

Var * InScope(Var * new_scope)
{
	Var * scope;
	scope = SCOPE;
	SCOPE = new_scope;
	return scope;
}

void ReturnScope(Var * prev)
{
	SCOPE = prev;
}

void EnterSubscope()
{
	Var * var;
	SCOPE_IDX++;
	var = NewVarWithIndex(SCOPE, SCOPE_NAME, SCOPE_IDX, TypeScope());
	SCOPE = var;
}

void ExitSubscope()
{
	SCOPE = SCOPE->scope;
}

void PrintScope(Var * scope)
/*
Purpose:
	Print variables in specified scope, and parent scopes.
	If scope is NULL, only global variables will be searched.
*/
{
	Var * var;
//	PrintFmt("===== %s =======\n", scope->name);
	FOR_EACH_LOCAL(scope, var)
		if (var->scope == scope) {
			PrintVar(var); PrintEOL();
		}
	NEXT_LOCAL
}

void PrintUserScope(Var * scope)
/*
Purpose:
	Print variables in specified scope, and parent scopes.
	If scope is NULL, only global variables will be searched.
*/
{
	Var * var;
//	PrintFmt("===== %s =======\n", scope->name);
	FOR_EACH_LOCAL(scope, var)
		if (FlagOff(var->submode, SUBMODE_SYSTEM) && FlagOn(var->submode, SUBMODE_USER_DEFINED)) {
			PrintVar(var); PrintEOL();
		}
		
	NEXT_LOCAL
}

/*************************************************************************************/

Type * CellType(Var * cell)
{
	if (cell == NULL) return &EMPTY;
	if (cell->mode == INSTR_VAR) {
		return cell->type;
	} else if (cell->mode == INSTR_ARRAY) {
		return cell->type;
	} else if (cell->mode == INSTR_INT || cell->mode == INSTR_RANGE || cell->mode == INSTR_TEXT) {
		return cell;
	} else if (FlagOn(INSTR_INFO[cell->mode].flags, INSTR_OPERATOR)) {
		return CellOp(cell->mode, CellType(cell->l), CellType(cell->r));
	}
	return NULL;
}

Bool CellIsValue(Var * var)
/*
Purpose:
	Cell is value, if it directly represents numeric, text or array value.
*/
{
	return var != NULL && (var->mode == INSTR_INT || var->mode == INSTR_TEXT || var->mode == INSTR_ARRAY || var->mode == INSTR_TYPE);
}

Bool TypeIsConst(Var * var)
/*
Purpose:
	If the cell is used as type, will it represent const (single value?).
*/
{
	switch(var->mode) {
	case INSTR_TYPE:
		return CellIsValue(var->possible_values);

	case INSTR_RANGE:
		return CellIsEqual(var->adr, var->var);

	case INSTR_VAR:
		return TypeIsConst(var->type);

	default:
		return CellIsValue(var);
	}
}

Bool CellIsConst(Var * var)
{
	if (var == NULL) return false;
	if (CellIsValue(var)) return true;
	if (var->mode == INSTR_VAR && TypeIsConst(var->type)) return true;
	return false;
}

Bool VarIsArray(Var * var)
{
	return var->type != NULL && var->type->variant == TYPE_ARRAY;
}

Bool VarIsStructElement(Var * var)
{
	return var->adr->type->variant == TYPE_STRUCT;
}

Bool VarIsArrayElement(Var * var)
{
	Var * adr;
	if (var == NULL || var->mode != INSTR_ELEMENT) return false;
	adr = var->adr;
//	if (adr == NULL) return false;			// TODO: ELEMENT with NULL adr?
	if (adr->mode == INSTR_DEREF) return true;
	return adr->type != NULL && adr->type->variant == TYPE_ARRAY;
//	return var != NULL && var->mode == INSTR_ELEMENT && var->adr != NULL && var->adr->type != NULL && (var->adr->type->variant == TYPE_ARRAY);
}

Var * VarNewLabel(char * name)
{
	Var * var;
	var = NewVar(NULL, name, &TLBL);
	return var;
}

Var * FindOrAllocLabel(char * name)
{
	Var * var = NULL;
	Var * proc;
	Var * scope;

	proc = VarProcScope();
	var = VarFind(proc, name);
	if (var == NULL) {
		scope = InScope(proc);
		var = VarNewLabel(name);
		ReturnScope(scope);
	}
	return var;
}


void VarToLabel(Var * var)
{
	var->type = &TLBL;
	var->mode = INSTR_VAR;
}

Var * VarNewTmpLabel()
{
	Var * var;
	TMP_LBL_IDX++;
	var = VarNewLabel(NULL);
	var->name = TMP_LBL_NAME;
	var->idx  = TMP_LBL_IDX;
	return var;
}

Var * VarFindScope2(Var * scope, char * name)
{
	Var * var = NULL;
	Var * s;
	for (s = scope; s != NULL; s = s->scope) {
		var = VarFind(s, name);
		if (var != NULL) break;
		// For procedures whose type has been defined using predefined type, try to find arguments from this type
		if (s->type->variant == TYPE_PROC && s->type != NULL /*&& s->type->owner != s*/) {			// ??????
			var = VarFind(s->type, name);
			if (var != NULL) break;
		}
	}
	return var;
}

Var * VarFind2(char * name)
/*
Purpose:
	Find variable in current scope.
*/
{
	return VarFindScope2(SCOPE, name);
}

Var * VarProcScope()
{
	Var * s;
	for (s = SCOPE; s != NULL; s = s->scope) {
		if (s->type != NULL && (s->type->variant == TYPE_PROC || s->type->variant == TYPE_MACRO)) break;
	}
	return s;
}

TypeVariant VarType(Var * var)
{
	if (var == NULL) return TYPE_VOID;
	if (var->mode == INSTR_INT) return TYPE_INT;
	if (var->mode == INSTR_RANGE) return TYPE_INT;
	if (var->mode == INSTR_TEXT) return TYPE_STRING;
	if (var->mode == INSTR_TYPE) return var->variant;
	if (var->mode == INSTR_VAR) return VarType(var->type);
	return TYPE_VOID;
}

Bool VarIsLabel(Var * var)
{
	return var->type->variant == TYPE_LABEL;
}

Bool VarIsUsed(Var * var)
{
	return var != NULL && (var->read > 0 || var->write > 0);
}

Bool IsVirtual(Var * cell)
{
	Var * type;
	if (cell == NULL || cell->mode != INSTR_VAR) return true;
	type = cell->type;
	return 
		CellIsConst(cell)
	 || VarIsReg(cell)
	 || VarIsRuleArg(cell)
	 || type->mode == INSTR_NULL
	 || (type->mode == INSTR_TYPE && (type->variant == TYPE_PROC || type->variant == TYPE_MACRO || type->variant == TYPE_LABEL || type->variant == TYPE_SCOPE || (type->variant == TYPE_TYPE && type->possible_values == NULL)))
	 || (type->mode == INSTR_VAR && IsVirtual(type));


}

void VarEmitAlloc()
/*
Purpose:	
	Emit instructions allocating variables, that are not placed at specific location.
*/
{
	Var * var, *cnst;
	Type * type;
	UInt32 size;	//, i;
	Var * dim1, * dim2;

	FOR_EACH_VAR(var)
		if (var->mode == INSTR_VAR && var->adr == NULL) {
			if (var->read > 0 || var->write > 0) {
				type = var->type;
				ASSERT(type != NULL);
				if (type != NULL) {

					if (type->mode == INSTR_TYPE && type->variant == TYPE_ARRAY && var->instr == NULL) {

						if (VarIsUsed(var)) {
					
							size = TypeSize(type);

							// Make array aligned (it type defines address, it is definition of alignment)

							if (type->adr != NULL) {
								EmitInstrOp(INSTR_ALIGN, NULL, type->adr, NULL);
							}

							ArraySize(type, &dim1, &dim2);

							if (dim2 != NULL) {
								EmitInstrOp(INSTR_LABEL, var, NULL, NULL);		// use the variable as label - this will set the address part of the variable
								EmitInstrOp(INSTR_ALLOC, var, dim1, dim2);
							} else {
								cnst = IntCellN(size);
								EmitInstrOp(INSTR_LABEL, var, NULL, NULL);		// use the variable as label - this will set the address part of the variable
								EmitInstrOp(INSTR_ALLOC, var, cnst, NULL);
							}
						}
					} else {
						if (!IsVirtual(var)) {
							if (InstrRule2(INSTR_ALLOC, var, NULL, NULL)) {
								EmitInstrOp(INSTR_ALLOC, var, NULL, NULL);
							}
						}
					}
				}
			}
		}
	NEXT_VAR
}

void VarGenerateArrays()
/*
Purpose:
	Generate instructions allocating space for arrays.
*/
{
	Var * var;
	Type * type;
	Rule * rule;
	Var * arr;

	// Generate initialized arrays, where location is not specified

	FOR_EACH_VAR(var)
		if (var->mode == INSTR_NAME && var->var != NULL && var->var->mode == INSTR_ARRAY) {
			arr = var->var;
			if (arr->instr != NULL && arr->adr == NULL) {
				if (VarIsUsed(var)) {
					type = var->type;
					// Make array aligned (it type defines address, it is definition of alignment)
					if (type->adr != NULL) {
						rule = InstrRule2(INSTR_ALIGN, NULL, type->adr, NULL);
						GenRule(rule, NULL, type->adr, NULL);
					}
					// Label & initializers
					GenLabel(var);
					GenBlock(arr->instr);
					arr->instr = NULL;
				}
			}
		}
	NEXT_VAR

	// Generate array indexes

	FOR_EACH_VAR(var)
		if (var->mode == INSTR_VAR /*|| var->mode == INSTR_INT*/) {
			type = var->type;
			if (type != NULL && type->variant == TYPE_ARRAY) {
				if (VarIsUsed(var)) {
					// If there is an index rule for the array, generate the index
					rule = InstrRule2(INSTR_ARRAY_INDEX, NULL, var, NULL);
					if (rule != NULL) {
						GenRule(rule, NULL, var, NULL);
					}
				}
			}
		}
	NEXT_VAR

	// Generate initialized arrays at specified addresses

	FOR_EACH_VAR(var)
		if (var->mode == INSTR_NAME && var->var != NULL && var->var->mode == INSTR_ARRAY) {
			arr = var->var;
			type = var->type;
			if (arr->instr != NULL && arr->adr != NULL && VarIsUsed(var)) {
//			if ((var->mode == INSTR_VAR || var->mode == INSTR_NAME) && var->instr != NULL && var->adr != NULL && VarIsUsed(var)) {
				rule = InstrRule2(INSTR_ORG, NULL, var->adr, NULL);
				GenRule(rule, NULL, var->adr, NULL);
				GenLabel(var);
				GenBlock(arr->instr);
			}
		}
	NEXT_VAR

}

void VarResetUse()
{
	Var * var;

	FOR_EACH_VAR(var)
		if (var->mode != INSTR_VAR || var->type->variant != TYPE_PROC) {
			var->read = 0;
			var->write = 0;
			var->flags = var->flags & (~(VarUninitialized|VarLoop|VarLoopDependent));
		}
	NEXT_VAR
}

void InitCPU()
/*
Purpose:
	Initialize array of registers.
	All variables that have adress in REGSET are registers.
*/
{
	Var * var;

	if (CPU->MEMORY == NULL) {
		CPU->REG_CNT = 0;

		if (CPU->SCOPE == NULL) {
			InternalError("CPU scope not found");
		}

		FOR_EACH_LOCAL(CPU->SCOPE, var)
			// Only variables without address are registers.
			// The variables with address are register sets.
			if (var->mode == INSTR_VAR && var->adr == NULL && TypeIsInt2(var->type)) {
				SetFlagOn(var->submode, SUBMODE_REG);
				CPU->REG[CPU->REG_CNT++] = var;
			}
		NEXT_LOCAL

		var = VarFind(CPU->SCOPE, "memory");
		if (var != NULL) {
			CPU->MEMORY = var->type;
		} else {
			InternalError("CPU.memory was not defined");
		}
	}
}

void VarResetRegUse()
{
	RegIdx i;
	for(i = 0; i<CPU->REG_CNT; i++) {
		SetFlagOff(CPU->REG[i]->flags, VarUsed);
	}
}

Bool VarIsReg(Var * var)
/*
Purpose:
	Return true, if this variable is register or is stored in register(s).	
*/
{
	if (var == NULL) return false;

	if (var->mode == INSTR_VAR) {
		if (FlagOn(var->submode, SUBMODE_REG)) return true;
		return VarIsReg(var->adr);		// variable address may be register
	} else if (var->mode == INSTR_TUPLE) {
		return VarIsReg(var->adr) || VarIsReg(var->var);
	}
	return false;
}

Bool ProcIsInterrupt(Var * proc)
{
	Type * base;
	base = proc->type;
	if (base != NULL) {
		while(base->type != NULL) base = base->type;
		if (base == INTERRUPT) return true;
	}
	return false;
}

void ProcUse(Var * proc, UInt8 flag)
/*
Purpose:
	Mark all used procedures starting with specified root procedure.
Arguments:
	flag		VarUsedInInterupt  This procedure is used from interrupt routine
				VarProcAddress	  Address of this procedure is used
*/
{
	Instr * i;
	InstrBlock * blk;
	Var * var, * label;
	UInt16 bmk;
	Loc loc;
	UInt16 n;

	loc.proc = proc;

	proc->read++;

	if (proc->instr == NULL) return;
	if (FlagOn(proc->flags, VarProcessed)) return;

//	PrintProc(proc);

	SetFlagOn(proc->flags, flag | VarProcessed);

	if (ProcIsInterrupt(proc)) {
		flag |= VarUsedInInterupt;
	}

	// Mark all defined labels (those defined with label instruction)

	for(blk = proc->instr; blk != NULL; blk = blk->next) {
		if (blk->label != NULL) {
			SetFlagOn(blk->label->flags, VarLabelDefined);
		}
		for(i = blk->first; i != NULL; i = i->next) {
			if (i->op == INSTR_LABEL) {
				SetFlagOn(i->result->flags, VarLabelDefined);
			}
		}
	}

	for(blk = proc->instr; blk != NULL; blk = blk->next) {
		for(i = blk->first, n=1; i != NULL; i = i->next, n++) {
			if (i->op == INSTR_CALL) {
				ProcUse(i->result, flag);
//				// Procedure has side-effect, if it call a procedure with side effect
//					if (FlagOn(i->result->submode, SUBMODE_OUT)) {
//					SetFlagOn(proc->submode, SUBMODE_OUT);
//				}
			} else {
				if (i->op != INSTR_LINE) {
					if (VarType(i->arg1) == TYPE_PROC) {
						ProcUse(i->arg1, flag | VarProcAddress);
					}
					if (VarType(i->arg2) == TYPE_PROC) {
						ProcUse(i->arg2, flag | VarProcAddress);
					}
					label = i->result;
					if (label != NULL && label->type->variant == TYPE_LABEL) {
						if (FlagOff(label->flags, VarLabelDefined)) {

							loc.blk = blk;
							loc.i   = i;
							bmk = SetBookmarkLine(&loc);

							var = VarFindTypeVariant(label->name, TYPE_LABEL);
							if (var != NULL) {
								ErrArg(var);
								SyntaxErrorBmk("Label [A] is defined in other procedure.\nIt is not possible to jump between procedures.", bmk);								
							} else {
								SyntaxErrorBmk("Label [A] is undefined", bmk);
							}
						}
					}
				}
			}
		}
	}
	SetFlagOff(proc->flags, VarProcessed);
}

Var * VarReplaceVar(Var * var, Var * from, Var * to)
/*
Purpose:
	Replace one variable by another.
	Variable may be used for example in array indexes, tuples etc.
	Replacement is performed 'in place'.
*/
{
	Var * l, * r;
	if (var == NULL) return NULL;
	if (var == from) return to;

	if (var->mode == INSTR_ELEMENT || var->mode == INSTR_TUPLE || var->mode == INSTR_RANGE) {
		l = VarReplaceVar(var->adr, from, to);
		r = VarReplaceVar(var->var, from, to);
		if (l != var->adr || r != var->var) var = NewOp(var->mode, l, r);

	} else if (var->mode == INSTR_DEREF) {
		l = VarReplaceVar(var->var, from, to);
		if (l != var->var) var = VarNewDeref(l);
	}
	return var;
}

void InstrBlockReplaceVar(InstrBlock * block, Var * from, Var * to)
/*
Purpose:
	Replace use of variable 'from' with variable 'to' in specified block.
	When this procedure ends, 'from' variable is no more referenced in the block.
*/
{
	Instr * i;
	InstrBlock * nb;
	for(nb = block; nb != NULL; nb = nb->next) {
		for (i = nb->first; i != NULL; i = i->next) {
			if (i->op != INSTR_LINE) {
				i->result = VarReplaceVar(i->result, from, to);
				i->arg1   = VarReplaceVar(i->arg1, from, to);
				i->arg2   = VarReplaceVar(i->arg2, from, to);
			}
		}
	}
}

void ProcReplaceVar(Var * proc, Var * from, Var * to)
/*
Purpose:
	Replace use of specific variable in a procedure by other variable.
*/
{
	InstrBlock * blk;

	for(blk = proc->instr; blk != NULL; blk = blk->next) {
		InstrBlockReplaceVar(blk, from, to);
	}
}

Bool VarIsFixed(Var * var)
/*
Purpose:
	Test, that variable is on fixed location.
	This means normal variable, argument or reference to array element with constant index.
*/
{
	if (var->mode == INSTR_VAR) return true;
	if (var->mode == INSTR_ELEMENT && var->var->mode == INSTR_INT) return true;		// access to constant array element
	return false;
}

Bool VarModifiesVar(Var * var, Var * test_var)
{
	Bool uses = false;
	if (var != NULL && test_var != NULL) {
		if (var->mode == INSTR_INT || test_var->mode == INSTR_INT) return false;

		if (var == test_var) {
			uses = true;
		} else {
			
			if (test_var->mode == INSTR_TUPLE) {
				return VarModifiesVar(var, test_var->adr) || VarModifiesVar(var, test_var->var);
			} else if (test_var->mode == INSTR_VAR && test_var->adr != NULL) {
				return VarModifiesVar(var, test_var->adr);
			}

			if (var->mode == INSTR_TUPLE) {
				return VarModifiesVar(var->adr, test_var) || VarModifiesVar(var->var, test_var);
			}

//			if (var->mode == INSTR_DEREF) {
//				uses = VarUsesVar(var->var, test_var);
//			} else if (var->mode == INSTR_ELEMENT || var->mode == INSTR_BYTE || var->mode == INSTR_TUPLE) {
//				uses = VarUsesVar(var->var, test_var) || VarUsesVar(var->adr, test_var);
//			} else if (var->adr != NULL) {
//				if (var->adr->mode != INSTR_INT) {
//					return VarUsesVar(var->adr, test_var);
//				}
//			}
		}
	}
	return uses;	
}

Bool VarUsesVar(Var * var, Var * test_var)
/*
Purpose:
	Return true, if the specified variable uses tested variable.
	It may either be same, or use the tested variable as index into array etc.
*/
{
	Bool uses = false;
	if (var != NULL && test_var != NULL) {

		if (test_var->mode == INSTR_INT) return false;

		if (var == test_var) {
			return true;
		} else {

			InstrInfo * ii = &INSTR_INFO[var->mode];

//			if (test_var->mode == INSTR_TUPLE) {
//				return VarUsesVar(var, test_var->adr) || VarUsesVar(var, test_var->var);
//			} else {

			if (var->mode == INSTR_DEREF) {
				return VarUsesVar(var->var, test_var);
			} else if (var->mode == INSTR_VAR) {
				if (var->adr != NULL) {
					if (var->adr->mode != INSTR_INT) {
						return VarUsesVar(var->adr, test_var);
					}
				}
			} else {
				if (ii->arg_type[1] == TYPE_ANY) {
					if (VarUsesVar(var->adr, test_var)) return true;
				}

				if (ii->arg_type[2] == TYPE_ANY) {
					if (VarUsesVar(var->var, test_var)) return true;
				}

//				} else if (var->mode == INSTR_ELEMENT || var->mode == INSTR_BYTE || var->mode == INSTR_TUPLE) {
//					return VarUsesVar(var->var, test_var) || VarUsesVar(var->adr, test_var);
			}
		}
		if (test_var->mode == INSTR_VAR && test_var->adr != NULL) {
			return VarUsesVar(var, test_var->adr);
		}
	}
	return false;	
}

Var * VarReg(Var * var)
/*
Purpose:
	Return register that is aliased by this variable or NULL.
*/
{
	Var * reg;

	reg = var;
	while(reg != NULL && reg->mode == INSTR_VAR) {
		if (FlagOn(reg->submode, SUBMODE_REG)) return reg;
		reg = reg->adr;
		if (reg != NULL && reg->mode == INSTR_TUPLE) return reg;
	}
	return var;
}

Bool VarIsLocal(Var * var, Var * scope)
{
	while (var != NULL) {
		if (var->scope == scope) return true;
		var = var->scope;
	}
	return false;
}

Var * NextArg(Var * proc, Var * arg, VarSubmode submode)
{
	Var * var = arg->next;
	while(var != NULL && (var->mode != INSTR_VAR || var->scope != arg->scope || FlagOff(var->submode, submode))) var = var->next;
	return var;
}

Var * FirstArg(Var * proc, VarSubmode submode)
{
	Var * var;

	if (proc->mode == INSTR_VAR) {
		// We may call the procedure using address stored in a variable, but we need to parse the arguments using
		// procedure itself.
		if (proc->type->variant == TYPE_ADR) {
			proc = proc->type->element;
		} else if (proc->type->variant == TYPE_PROC) {
			proc = proc->type;
		}
	}

	// If this procedure type is defined using shared definition, use the definition to find the arguments

//	if (owner != NULL && owner != proc) {
//		proc = owner;
//	}
	var = proc->subscope;
	while(var != NULL && (var->mode != INSTR_VAR || var->scope != proc || FlagOff(var->submode, submode))) var = var->next;
	return var;
}


Var * VarField(Var * var, char * fld_name)
/*
Purpose:
	Return property of variable.
	Following properties are supported:

	min
	max
	step
*/
{
	Var * fld = NULL;
	Type * type;
	TypeVariant vtype;

	type = var->type;
	vtype = type->variant;

	if (StrEqual(fld_name, "min")) {
		fld = CellMin(type);
	} else if (StrEqual(fld_name, "max")) {
		fld = CellMax(type);
	} else if (var->mode == INSTR_TYPE && vtype == TYPE_ARRAY) {
		if (StrEqual(fld_name, "step")) {
			fld = IntCellN(type->step);
		}
	}

	if (fld == NULL) {
		fld = VarFind(var, fld_name);
	}
	return fld;
}

Var * VarEvalConst(Var * var)
/*
Purpose:
	Evaluate the variable, trying to make constant of it.
	Return the same variable, if no evaluating is possible.
*/
{
	Var * res = var;
	BigInt * a, * idx;
	BigInt t1, t2;

	if (res != NULL) {

		// Get the most elementary constant out of the variable

		while (res->mode == INSTR_NAME && res->type->variant != TYPE_ARRAY && res->var != NULL) {
			res = res->var;
		}

		// Getting n-th byte is same as special variant of AND
		if (res->mode == INSTR_BYTE) {
			a   = IntFromCell(var->adr);
			idx = IntFromCell(var->var);

			if (a != NULL && idx != NULL) {
				// t3 = (a >> (idx * 8)) & 0xff;
				IntSet(&t1, idx);
				IntMulN(&t1, 8);
				IntShr(&t2, a, &t1);
				IntAndN(&t2, 0xff);

				//a = (a >> (idx * 8)) & 0xff;
				res = IntCell(&t2);

				IntFree(&t1);
				IntFree(&t2);			
			}
		}
	}
	return res;
}

void VarLet(Var * var, Var * val)
/*
Purpose:
	Set the variable var to specified value.
	The variable must be of type INSTR_NAME or INSTR_VAR and the value is set to var property.
*/
{
	if (var != NULL && val != NULL) {
		ASSERT(var->mode == INSTR_VAR);

		if (CellIsValue(val)) {
			var->var = val;
		} else {
			var->var = val->var;
		}

	}
}

Bool VarIsInArg(Var * var)
{
	return FlagOn(var->submode, SUBMODE_ARG_IN);
}

Bool VarIsOutArg(Var * var)
{
	return FlagOn(var->submode, SUBMODE_ARG_OUT);
}

Bool VarIsArg(Var * var)
{
	return FlagOn(var->submode, SUBMODE_ARG_IN | SUBMODE_ARG_OUT);
}


Var * VarFindAssociatedConst(Var * var, char * name)
{
	if (var == NULL) return NULL;
	return VarFind(var->type, name);
}


Bool VarIsParam(Var * var)
{
	return CellIsConst(var) && FlagOn(var->submode, SUBMODE_PARAM);
}

void VarInit()
{

	TMP_IDX = 1;
	TMP_LBL_IDX = 0;
	SCOPE_IDX = 0;

	CELL_BLOCKS = LAST_CELL_BLOCK = NewCellBlock();

	// Alloc rule procedure and rule arguments (rule arguments are local arguments of INSTR_PROC)

	CPU = &CPUS[0];

	IntCellInit();

}