/*

Type operations

(c) 2010 Rudolf Kudla 
Licensed under the MIT license: http://www.opensource.org/licenses/mit-license.php

*/

//TODO: Detect use of uninitialized array element. (Do not report error, if there is a chance, it was initialized).

#include "language.h"
#ifdef __Darwin__
#include "limits.h"
#endif

//#define TYPE_CONST_COUNT 1024

GLOBAL Var  EMPTY;		// No value at all
GLOBAL Type * TTYPE;
GLOBAL Type TINT;		// used for int constants
GLOBAL Type TSTR;
GLOBAL Type TLBL;
GLOBAL Type TBYTE;		//0..255
GLOBAL Type TSCOPE;
GLOBAL Type * TUNDEFINED;

//GLOBAL Type * TCONST[TYPE_CONST_COUNT];		// We keep the reference to array of constant integer types 0..1023

void VarRange(Var * var, BigInt ** p_min, BigInt ** p_max);


void TypeMark(Type * type)
{
	if (type != NULL) {
//		SetFlagOn(type->flags, TypeUsed);
		TypeMark(type->type);
		if (type->variant == TYPE_ARRAY) {
			TypeMark(type->index);
			TypeMark(type->element);
		} else if (type->variant == TYPE_VARIANT || type->variant == TYPE_TUPLE) {
			TypeMark(type->left);
			TypeMark(type->right);
		}
	}
}
/*
void TypeGarbageCollect()
{
	Var * var;
	Type * type;
	UInt8 i;
	TypeBlock * tb;

	// Mark all types as unused

	for(tb = &TYPES; tb != NULL; tb = tb->next) {
		for(i=0, type = tb->types; i<TYPE_BLOCK_CAPACITY; i++, type++) {
//			SetFlagOff(type->flags, TypeUsed);
		}
	}

	// Mark all types used by variables, rules and instructions

	for (var = VARS; var != NULL; var = var->next) {
		TypeMark(var->type);
	}

	RulesGarbageCollect();

}
*/

Type * TypeAlloc(TypeVariant variant)
{
	Type * type;
	type = NewCell(INSTR_TYPE);
	type->submode = 0;
	type->type = NULL;
	type->variant = variant;
	type->flexible = false;
	return type;
}

/*

Integer types.

*/

Type * TypeType(Type * restriction)
/*
Purpose:
	Alloc type of type.
*/
{
	Type * type;
	if (restriction == NULL) {
		type = TTYPE;
	} else {
		type = TypeAlloc(TYPE_TYPE);
		type->left = restriction;
	}
	return type;
}

Type * TypeAdrOf(Type * element)
/*
Purpose:
	Alloc type as "adr of <element>".
	If the element is not specified, it is "adr of <memory>".
*/
{
	Type * type = TypeAlloc(TYPE_ADR);
	if (element == NULL) {
		if (CPU->MEMORY == NULL) {
			InitCPU();
		}
		element = CPU->MEMORY;
	}
	type->element = element;
	return type;
}

Type * TypeCopy(Type * base)
{
	Type * type = NewCell(INSTR_TYPE);
	memcpy(type, base, sizeof(Type));
	return type;
}

Type * TypeDerive(Type * base)
{
	Type * type = TypeCopy(base);
	type->type = base;
	return type;
}


void TypeAddConst(Type * type, Var * var)
/*
Purpose:
	Add specified variable as associated constant to type.
	The added variable must be of type INSTR_VAR.
*/
{
	Var * c;
	ASSERT(type->mode == INSTR_TYPE);
	ASSERT(var->mode == INSTR_VAR);
	c = var->type;
	ASSERT(c->mode == INSTR_INT);
	
	type->possible_values = VarUnion(type->possible_values, var->type);
}


UInt32 TypeAdrSize()
{
	//TODO: should be platform defined
	return 2;
}

Type * TypeByte()
{
	return &TBYTE;
}

Type * TypeScope()
{
	return &TSCOPE;
}

Type * TypeTuple(Type * left, Type * right)
{
	Type * type = TypeAlloc(TYPE_TUPLE);
	type->left  = left;
	type->right = right;
	return type;
}

Type * TypeArray(Type * index, Type * element)
{
	Type * type = TypeAlloc(TYPE_ARRAY);
	type->index = index;
	type->element = element;
	type->step = TypeSize(element);
	return type;
}

UInt32 TypeStructAssignOffsets(Type * type)
/*
Purpose:
	Assign offsets to elements of structure.
*/
{
	UInt32 offset = 0;
	Var * item;
	FOR_EACH_LOCAL(type, item)
		if (item->mode == INSTR_VAR) {
			if (item->adr == NULL) {
				item->adr = IntCellN(offset);
				offset += TypeSize(item->type);
			}
		}
	NEXT_LOCAL
	return offset;			// offset now contains total size of structure
}

void TypeInit()
{

//	FREE_TYPE = NULL;
//	TypeInitBlock(&TYPES);

	EMPTY.mode = INSTR_EMPTY;

	TUNDEFINED = NewCell(INSTR_TYPE);
	TUNDEFINED->variant = TYPE_UNDEFINED;

	TTYPE = NewCell(INSTR_TYPE);
	TTYPE->variant = TYPE_TYPE;
	TTYPE->left = NULL;

	TINT.mode    = INSTR_TYPE;
	TINT.variant = TYPE_INT;
	TINT.range.min = -(long)2147483648L;
	TINT.range.max = 2147483647L;
	TINT.type      = NULL;

	TBYTE.mode    = INSTR_TYPE;
	TBYTE.variant = TYPE_INT;
	TBYTE.range.min = 0;
	TBYTE.range.max = 255;
	TBYTE.type      = NULL;

	TSTR.mode    = INSTR_TYPE;
	TSTR.variant = TYPE_STRING;
	TSTR.type      = NULL;

	TLBL.mode    = INSTR_TYPE;
	TLBL.variant = TYPE_LABEL;
	TLBL.range.min = -(long)2147483648L;
	TLBL.range.max = 2147483647L;
	TLBL.type     = NULL;

	TSCOPE.mode    = INSTR_TYPE;
	TSCOPE.variant = TYPE_SCOPE;
	TSCOPE.type   = NULL;

}

Bool TypeIsInt2(Type * type)
{
	return type != NULL && (type->mode == INSTR_INT || type->mode == INSTR_RANGE || (type->mode == INSTR_TYPE && type->variant == TYPE_INT));
}

Bool TypeIsInt(Type * type)
{
	return type != NULL && type->variant == TYPE_INT;
}


void PrintTypeNoBrace(Type * type)
{
	if (type == NULL) { Print("NULL"); return; }
	switch(type->variant) {
	case TYPE_TUPLE:
		PrintType(type->left); Print(" ,"); PrintType(type->right);
		break;
	default:
		PrintType(type);
	}
}

void PrintType(Type * type)
{
	if (type == NULL) { Print("NULL"); return; }
	switch(type->variant) {
	case TYPE_INT:
		Print("int ");
		break;

	case TYPE_TYPE:
		Print("type ");
		break;

	case TYPE_ADR:
		Print("adr of ");
		PrintType(type->element);
		break;

	case TYPE_ARRAY:
		Print("array (");
		PrintTypeNoBrace(type->index);
		Print(") of ");
		PrintType(type->element);
		break;

	case TYPE_PROC:
		Print("proc");
		break;

	case TYPE_VARIANT:
		PrintType(type->left); Print(" | "); PrintType(type->right);
		break;

	case TYPE_TUPLE:
		Print("("); PrintTypeNoBrace(type); Print(")");
		break;

	case TYPE_MACRO:
		Print("proc");
		break;

		default:
		break;
	}

	if (type->possible_values != NULL) {
		PrintVar(type->possible_values);
	}
}

extern char * TMP_NAME;

void PrintVars(Var * proc)
{
	Var * var;
	Type * type;

	FOR_EACH_LOCAL(proc, var)
		if (var->mode == INSTR_SCOPE) {
			PrintVars(var);
		} else {
			if (var->name != NULL && var->name != TMP_NAME && FlagOff(var->submode, SUBMODE_SYSTEM) && var->mode == INSTR_VAR) {
				type = var->type;
				if (type != NULL && type->variant == TYPE_LABEL) continue;
				PrintFmt("%s: ", var->name);
				PrintType(var->type);
				Print("\n");
			}
		}
	NEXT_LOCAL
}


#define TYPE_IS_UNDEFINED(t)  (t == NULL)



//$R

Type * FindType(Loc * loc, Var * var, Bool report_errors);

UInt16 g_fb_level;

//#define TRACE_INFER 1