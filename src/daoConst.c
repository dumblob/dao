/*=========================================================================================
  This file is a part of a virtual machine for the Dao programming language.
  Copyright (C) 2006-2010, Fu Limin. Email: fu@daovm.net, limin.fu@yahoo.com

  This software is free software; you can redistribute it and/or modify it under the terms 
  of the GNU Lesser General Public License as published by the Free Software Foundation; 
  either version 2.1 of the License, or (at your option) any later version.

  This software is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
  See the GNU Lesser General Public License for more details.
  =========================================================================================*/

#include"daoConst.h"
#include"daoOpcode.h"

static const char *const vmOperNames[] = {
	"NOP", "DATA", "GETC", "GETV", "GETI", "GETF", "GETMF",
	"SETV", "SETI", "SETF", "SETMF",
	"LOAD", "CAST", "MOVE",
	"NOT", "UNMS", "BITREV",
	"ADD", "SUB", "MUL", "DIV", "MOD", "POW",
	"AND", "OR", "LT", "LE", "EQ", "NE",
	"BITAND", "BITOR", "BITXOR", "BITLFT", "BITRIT",
	"CHECK", "PAIR", "TUPLE",
	"LIST", "MAP", "HASH", "ARRAY", "MATRIX", "CURRY", "MCURRY",
	"GOTO", "SWITCH", "CASE", "ITER", "TEST", 
	"MATH", "FUNCT", "CALL", "MCALL", 
	"CLOSE", "CRRE", "JITC", "JOINT", "RETURN", "YIELD", "DEBUG", "SECT",
	"GETC_I", "GETC_F", "GETC_D",
	"GETV_I", "GETV_F", "GETV_D",
	"SETV_II", "SETV_IF", "SETV_ID", 
	"SETV_FI", "SETV_FF", "SETV_FD", 
	"SETV_DI", "SETV_DF", "SETV_DD",
	"MOVE_II", "MOVE_IF", "MOVE_ID",
	"MOVE_FI", "MOVE_FF", "MOVE_FD",
	"MOVE_DI", "MOVE_DF", "MOVE_DD",
	"MOVE_CC", "MOVE_SS", "MOVE_PP",
	"NOT_I", "NOT_F", "NOT_D", 
	"UNMS_I", "UNMS_F", "UNMS_D",
	"BITREV_I", "BITREV_F", "BITREV_D",
	"UNMS_C",
	"ADD_III", "SUB_III", "MUL_III", "DIV_III", "MOD_III", "POW_III",
	"AND_III", "OR_III", "LT_III", "LE_III", "EQ_III", "NE_III", 
	"BITAND_III", "BITOR_III", "BITXOR_III", "BITLFT_III", "BITRIT_III", 
	"ADD_FFF", "SUB_FFF", "MUL_FFF", "DIV_FFF", "MOD_FFF", "POW_FFF",
	"AND_FFF", "OR_FFF", "LT_FFF", "LE_FFF", "EQ_FFF", "NE_FFF", 
	"BITAND_FFF", "BITOR_FFF", "BITXOR_FFF", "BITLFT_FFF", "BITRIT_FFF", 
	"ADD_DDD", "SUB_DDD", "MUL_DDD", "DIV_DDD", "MOD_DDD", "POW_DDD",
	"AND_DDD", "OR_DDD", "LT_DDD", "LE_DDD", "EQ_DDD", "NE_DDD", 
	"BITAND_DDD", "BITOR_DDD", "BITXOR_DDD", "BITLFT_DDD", "BITRIT_DDD", 
	"ADD_FNN", "SUB_FNN", "MUL_FNN", "DIV_FNN", "MOD_FNN", "POW_FNN",
	"AND_FNN", "OR_FNN", "LT_FNN", "LE_FNN", "EQ_FNN", "NE_FNN", 
	"BITLFT_FNN", "BITRIT_FNN", 
	"ADD_DNN", "SUB_DNN", "MUL_DNN", "DIV_DNN", "MOD_DNN", "POW_DNN",
	"AND_DNN", "OR_DNN", "LT_DNN", "LE_DNN", "EQ_DNN", "NE_DNN", 
	"BITLFT_DNN", "BITRIT_DNN", 
	"ADD_SS", "LT_SS", "LE_SS", "EQ_SS", "NE_SS",
	"GETI_LI", "SETI_LI",
	"GETI_SI", "SETI_SII",
	"GETI_LII", "GETI_LFI", "GETI_LDI", "GETI_LSI",
	"SETI_LIII", "SETI_LIIF", "SETI_LIID",
	"SETI_LFII", "SETI_LFIF", "SETI_LFID",
	"SETI_LDII", "SETI_LDIF", "SETI_LDID", "SETI_LSIS",
	"GETI_AII", "GETI_AFI", "GETI_ADI",
	"SETI_AIII", "SETI_AIIF", "SETI_AIID",
	"SETI_AFII", "SETI_AFIF", "SETI_AFID",
	"SETI_ADII", "SETI_ADIF", "SETI_ADID",
	"GETI_TI", "SETI_TI", 
	"GETF_T", "GETF_TI", "GETF_TF", "GETF_TD", "GETF_TS",
	"SETF_T",
	"SETF_TII", "SETF_TIF", "SETF_TID",
	"SETF_TFI", "SETF_TFF", "SETF_TFD",
	"SETF_TDI", "SETF_TDF", "SETF_TDD", "SETF_TSS",
	"ADD_CC", "SUB_CC", "MUL_CC", "DIV_CC", 
	"GETI_ACI", "SETI_ACI",
	"GETI_AM", "SETI_AM",
	"GETF_M",
	"GETF_KC", "GETF_KG", "GETF_OC", "GETF_OG", "GETF_OV", 
	"SETF_KG", "SETF_OG", "SETF_OV",
	"GETF_KCI", "GETF_KGI",
	"GETF_OCI", "GETF_OGI", "GETF_OVI",
	"GETF_KCF", "GETF_KGF",
	"GETF_OCF", "GETF_OGF", "GETF_OVF",
	"GETF_KCD", "GETF_KGD",
	"GETF_OCD", "GETF_OGD", "GETF_OVD",
	"SETF_KGII", "SETF_OGII", "SETF_OVII",
	"SETF_KGIF", "SETF_OGIF", "SETF_OVIF",
	"SETF_KGID", "SETF_OGID", "SETF_OVID",
	"SETF_KGFI", "SETF_KGFI", "SETF_OVFI", 
	"SETF_KGFF", "SETF_KGFF", "SETF_OVFF",
	"SETF_KGFD", "SETF_KGFD", "SETF_OVFD",
	"SETF_KGDI", "SETF_OGDI", "SETF_OVDI",
	"SETF_KGDF", "SETF_OGDF", "SETF_OVDF",
	"SETF_KGDD", "SETF_OGDD", "SETF_OVDD",
	"TEST_I", "TEST_F", "TEST_D",
	"CALL_CF", "CALL_CMF", "CALL_TC", "MCALL_TC",
	"GOTO",
	"???",

	/* for compiling only */
	"idx" ,
	"incr" ,
	"decr" ,
	"comma" , 
	"if" ,
	"elif" ,
	"else" ,
	"while_aux" ,
	"while" ,
	"for_aux" ,
	"for_step" ,
	"for" ,
	"do" ,
	"until" ,
	"dowhile" ,
	"casetag" ,
	"default" ,
	"break" ,
	"skip" ,
	"lbra" ,
	"rbra" ,
	"lbra2" ,
	"rbra2" ,
	"try" ,
	"retry" ,
	"raise" ,
	"rescue" ,
	"label" ,
	"scbegin" ,
	"scend" ,
	"enum" ,
	"setvg_aux" ,
	"refer" ,
	"unused"
};
const char* getOpcodeName( int opc )
{
	if( opc >= 0 && opc < DVM_NULL ) return vmOperNames[ opc ];
	if( opc > DVM_NULL && opc <= DVM_UNUSED ) return vmOperNames[ opc-DVM_IDX+DVM_NULL+1 ];
	return "???";
}

static const char* const daoCtInfo[] = 
{
	"",
	"internal error",
	"exceed parameter number limit",
	"Undefined symbol",
	"Unpaired tokens",
	"is expected",
	"is not expected",
	"is not declared",
	"has been previously defined",
	"No method to use from",
	"Symbol possibly undefined",
	"Symbol not defined",
	"Symbol was defined",
	"Need symbol of constant",
	"Need symbol of class",
	"Need symbol of class or C type",
	"Need symbol of interface",
	"Need bindable type (Dao class or C type)",
	"Need string token",
	"Need valid name" ,
	"Token not found",
	"Token not expected",
	"Tokens not paired",
	"Tokens not expected",
	"Need constant expression for number",
	"Need constant expression",
	"Constant cannot be modified",
	"Undefined scope name",
	"Invalid path setting",
	"Invalid storage combination",
	"Invalid type name",
	"Invalid type form",
	"Invalid expression",
	"Invalid statement",
	"Invalid definition",
	"Invalid enum definition",
	"Invalid class definition",
	"Invalid function definition",
	"Invalid interface definition",
	"Invalid parent class",
	"Invalid use statement",
	"Invalid interface binding",
	"Invalid type define",
	"Invalid parameter list for",
	"Invalid parameter",
	"Parameters not separated",
	"Variable-length parameter list not final",
	"Explicit type not expected",
	"Parameter need explicit type",
	"Parameter need default value",
	"Default parameter is not constant",
	"Invalid default parameter",
	"Default value not matching parameter type",
	"Too many parameters",
	"Invalid return type",
	"Statement in class body",
	"Statement in interface body",
	"Statement used out of context",
	"Type not matching",
	"Type of given value",
	"Type of expected value",
	"Finalized class cannot be derived",
	"Function need return type",
	"Invalid operator for overloading",
	"Constructor cannot return",
	"Method need implementation",
	"Method not declared",
	"Method signature not matching",
	"Invalid case statement",
	"Case value not constant",
	"default parameter is not const",
	"invalid parameter list",
	"parameter number not correct",
	"invalid named parameter",
	"invalid parameter default",
	"my, is used in non-constructor routine",
	"uncompatible variable prefix",
	"invalid for statement",
	"invalid for-in-do statement",
	"invalid case for switch",
	"case with non-const value",
	"case is not directly nested by switch",
	"invalid load statement",
	"invalid path for loading",
	"invalid variable name for importing",
	"invalid module name for loading",
	"loading failed",
	"loading cancelled",
	"variable(s) not found for importing",
	"redundant code in load statement",
	"attempt to init const var from non-const expr",
	"attempt to modify const",
	"invalid expression",
	"invalid enumeration",
	"exceed matrix enumeration limit",
	"invalid multi-assignment",
	"invalid string literal",
	"un-paired quotation symbol",
	"unknow operator",
	"unknow escape char",
	"for assignment inside an expression, use :=",
	"different version of dao.h is used",
	"invalid Dao script embedding",
	"invalid syntax",
	"unmatched assignment",
	"re-definition of variable",
	"invalid syntax definition",
	"invalid first token in macro defintion",
	"invalid macro group opening",
	"unknown macro variable type",
	"unknown macro repeat type",
	"unknown special token in the macro",
	"invalid indentation marker in the macro",
	"re-definition of special marker",
	"undefine macro marker",
	"unmatched grouping level" ,
	"invalid type form",
	"invalid type name",
	"invalid constant expression",
	"deriving from a finalized class",
	"not permited in the safe running mode",
	"type not matched",
	"interface binding failed",
	"is not enabled",
	"obsolete syntax",
	""
};

const char* getCtInfo( int tp )
{
	if( tp < 0 || tp >= DAO_CTW_END ) return "";
	return daoCtInfo[ tp ];
}
const char* getRtInfo( int tp )
{
	return "";
}

const char* const daoExceptionName[] =
{
	"ExceptionNone" ,
	"ExceptionAny" ,
	"ExceptionError" ,
	"ExceptionWarning" ,
	"ErrorField" ,
	"ErrorFieldNotExist" ,
	"ErrorFieldNotPermit" ,
	"ErrorFloat" ,
	"ErrorFloatDivByZero" ,
	"ErrorFloatOverFlow" ,
	"ErrorFloatUnderFlow" ,
	"ErrorIndex" ,
	"ErrorIndexOutOfRange" ,
	"ErrorKey" ,
	"ErrorKeyNotExist" ,
	"ErrorParam" ,
	"ErrorSyntax" ,
	"ErrorType" ,
	"ErrorValue" ,
	"WarningSyntax" ,
	"WarningValue" ,
	""
};
