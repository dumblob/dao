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

#include"string.h"
#include"ctype.h"
#include"locale.h"
/* #include"unistd.h" */

#include"daoNamespace.h"
#include"daoVmspace.h"
#include"daoParser.h"
#include"daoStream.h"
#include"daoRoutine.h"
#include"daoNumeric.h"
#include"daoClass.h"
#include"daoObject.h"
#include"daoRoutine.h"
#include"daoRegex.h"
#include"daoStdlib.h"
#include"daoContext.h"
#include"daoProcess.h"
#include"daoGC.h"
#include"daoSched.h"
#include"daoAsmbc.h"

#ifdef DAO_WITH_THREAD
#include"daoThread.h"
#endif

#ifdef DAO_WITH_NETWORK
void DaoProxy_Init( DaoVmSpace *vmSpace );
void DaoNetwork_Init( DaoVmSpace *vms, DaoNameSpace *ns );
extern DaoTypeBase libNetTyper;
#endif

extern int ObjectProfile[100];

DAO_DLL DaoAPI __dao;

DaoConfig daoConfig = 
{
  1,    /*cpu*/
  1,    /*jit*/
  0,    /*safe*/
  1,    /*typedcode*/
  1,    /*incompile*/
  0,    /*iscgi*/
  8,    /*tabspace*/
  0     /*chindent*/
};

DaoVmSpace   *mainVmSpace = NULL;
DaoVmProcess *mainVmProcess = NULL;

static int TestPath( DaoVmSpace *vms, DString *fname );

extern ullong_t FileChangedTime( const char *file );

static const char* const daoFileSuffix[] = { ".dao.o", ".dao.s", ".dao", DAO_DLL_SUFFIX };
enum{ 
  DAO_MODULE_NONE, 
  DAO_MODULE_DAO_O,
  DAO_MODULE_DAO_S,
  DAO_MODULE_DAO,
  DAO_MODULE_DLL
};

static const char *const copy_notice =
"\n  Dao Virtual Machine (" DAO_VERSION ", " __DATE__ ")\n"
"  Copyright(C) 2006-2010, Fu Limin.\n"
"  Dao can be copied under the terms of GNU Lesser General Public License.\n"
"  Dao Language website: http://www.daovm.net\n\n";

static const char *const cmd_help =
"\n Usage: dao [options] script_file\n"
" Options:\n"
"   -h, --help:           print this help information;\n"
"   -v, --version:        print version information;\n"
"   -s, --safe:           run in safe mode;\n"
"   -d, --debug:          run in debug mode;\n"
"   -i, --ineractive:     run in interactive mode;\n"
"   -l, --list-bc:        print compiled bytecodes;\n"
"   -T, --no-typed-code:  no typed VM codes;\n"
"   -J, --no-jit:         no just-in-time compiling;\n"
"   -n, --incr-comp:      incremental compiling;\n"
"   -p, --proc-name:      process name for communication.\n\n";
/*
"   -s, --assembly:    generate assembly file;\n"
"   -b, --bytecode:    generate bytecode file;\n"
"   -c, --compile:     compile to bytecodes; (TODO)\n"
*/

static const char* daoScripts = 
"class FutureValue {\n"
"    var Value;\n"
"    routine FutureValue( value ){ Value = value; }\n"
"}\n"
"\n"
"class Exception {\n"
"  routine Exception( content='' ){ Content = content }\n"
"  protected\n"
"    var Rout = '';\n"
"    var File = '';\n"
"    var FromLine = 0;\n"
"    var ToLine = 0;\n"
"  public\n"
"    var Name  = 'Exception';\n"
"    var Content : any = 'undefined exception';\n"
"}\n"
"\n"
"class Exception::None : Exception {\n"
"  Name := 'ExceptionNone';\n"
"  Content := 'none exception' }\n"
"class Exception::Any : Exception {\n"
"  Name := 'ExceptionAny';\n"
"  Content := 'any or none exception' }\n"
"class Error : Exception {\n"
"  Name := 'Error';\n"
"  Content := 'undefined error' }\n"
"class Warning : Exception {\n"
"  Name := 'Warning';\n"
"  Content := 'undefined error' }\n"
"\n"
"global ExceptionNone = Exception::None{};\n"
"global ExceptionAny = Exception::Any{};\n"
"global ExceptionError = Error{};\n"
"global ExceptionWarning = Warning{};\n"
"\n"
"class Error::Field : Error {\n"
"  Name := 'ErrorField';\n"
"  Content := 'invalid field accessing' }\n"
"class Error::Field::NotExist : Error::Field {\n"
"  Name := 'ErrorFieldNotExist';\n"
"  Content := 'field not exist' }\n"
"class Error::Field::NotPermit : Error::Field {\n"
"  Name := 'ErrorFieldNotPermit';\n"
"  Content := 'field not permit' }\n"
"class Error::Float : Error {\n"
"  Name := 'ErrorFloat';\n"
"  Content := 'invalid floating point operation' }\n"
"class Error::Float::DivByZero : Error::Float {\n"
"  Name := 'ErrorFloatDivByZero';\n"
"  Content := 'division by zero' }\n"
"class Error::Float::OverFlow : Error::Float {\n"
"  Name := 'ErrorFloatOverFlow';\n"
"  Content := 'floating point overflow' }\n"
"class Error::Float::UnderFlow : Error::Float {\n"
"  Name := 'ErrorFloatUnderFlow';\n"
"  Content := 'floating point underflow' }\n"
"class Error::Index : Error {\n"
"  Name := 'ErrorIndex';\n"
"  Content := 'invalid index' }\n"
"class Error::Index::OutOfRange : Error::Index {\n"
"  Name := 'ErrorIndexOutOfRange';\n"
"  Content := 'index out of range' }\n"
"class Error::Key : Error {\n"
"  Name := 'ErrorKey';\n"
"  Content := 'invalid key' }\n"
"class Error::Key::NotExist : Error::Key {\n"
"  Name := 'ErrorKeyNotExist';\n"
"  Content := 'key not exist' }\n"
"class Error::Param : Error {\n"
"  Name := 'ErrorParam';\n"
"  Content := 'invalid parameter list for the call' }\n"
"class Error::Syntax : Error {\n"
"  Name := 'ErrorSyntax';\n"
"  Content := 'invalid syntax' }\n"
"class Error::Type : Error {\n"
"  Name := 'ErrorType';\n"
"  Content := 'invalid variable type for the operation' }\n"
"class Error::Value : Error {\n"
"  Name := 'ErrorValue';\n"
"  Content := 'invalid variable value for the operation' }\n"
"\n"
"global ErrorField = Error::Field{};\n"
"global ErrorFieldNotExist = Error::Field::NotExist{};\n"
"global ErrorFieldNotPermit = Error::Field::NotPermit{};\n"
"global ErrorFloat = Error::Float{};\n"
"global ErrorFloatDivByZero = Error::Float::DivByZero{};\n"
"global ErrorFloatOverFlow = Error::Float::OverFlow{};\n"
"global ErrorFloatUnderFlow = Error::Float::UnderFlow{};\n"
"global ErrorIndex = Error::Index{};\n"
"global ErrorIndexOutOfRange = Error::Index::OutOfRange{};\n"
"global ErrorKey = Error::Key{};\n"
"global ErrorKeyNotExist = Error::Key::NotExist{};\n"
"global ErrorParam = Error::Param{};\n"
"global ErrorSyntax = Error::Syntax{};\n"
"global ErrorType = Error::Type{};\n"
"global ErrorValue = Error::Value{};\n"
"\n"
"class Warning::Syntax : Warning {\n"
"  Name := 'WarningSyntax';\n"
"  Content := 'invalid syntax' }\n"
"class Warning::Value : Warning {\n"
"  Name := 'WarningValue';\n"
"  Content := 'invalid value for the operation' }\n"
"global WarningSyntax = Warning::Syntax{};\n"
"global WarningValue = Warning::Value{};";

/* TODO: modify proxy_receive() so that it can be run in native thread: */
static const char* daoProxyScripts =
"routine Proxy_Receiver(){ proxy_receive() }\n"
"Proxy_Receiver() async join;";
/* need wrapping so that proxy_receive() can access the namespace */

extern DaoTypeBase  baseTyper;
extern DaoTypeBase  numberTyper;
extern DaoTypeBase  stringTyper;
extern DaoTypeBase  longTyper;
extern DaoTypeBase  listTyper;
extern DaoTypeBase  mapTyper;
extern DaoTypeBase  pairTyper;
extern DaoTypeBase  streamTyper;
extern DaoTypeBase  routTyper;
extern DaoTypeBase  funcTyper;
extern DaoTypeBase  classTyper;
extern DaoTypeBase  objTyper;
extern DaoTypeBase  nsTyper;
extern DaoTypeBase  tupleTyper;

extern DaoTypeBase  numarTyper;
extern DaoTypeBase  comTyper;
extern DaoTypeBase  abstypeTyper;
extern DaoTypeBase  curryTyper;
extern DaoTypeBase  rgxMatchTyper;

extern DaoTypeBase mutexTyper;
extern DaoTypeBase condvTyper;
extern DaoTypeBase semaTyper;
extern DaoTypeBase threadTyper;
extern DaoTypeBase thdMasterTyper;

extern DaoTypeBase macroTyper;
extern DaoTypeBase regexTyper;
extern DaoTypeBase ctxTyper;
extern DaoTypeBase vmpTyper;
static DaoTypeBase vmsTyper;

DaoTypeBase* DaoVmSpace_GetTyper( short type )
{
  switch( type ){
  case DAO_INTEGER   :  return & numberTyper;
  case DAO_FLOAT     :  return & numberTyper;
  case DAO_DOUBLE    :  return & numberTyper;
  case DAO_STRING    :  return & stringTyper;
  case DAO_COMPLEX   :  return & comTyper;
  case DAO_LONG      :  return & longTyper;
  case DAO_LIST      :  return & listTyper;
  case DAO_MAP      :  return & mapTyper;
  case DAO_PAIR     :  return & pairTyper;
  case DAO_PAR_NAMED :  return & pairTyper;
  case DAO_TUPLE     : return & tupleTyper;
#ifdef DAO_WITH_NUMARRAY
  case DAO_ARRAY  :  return & numarTyper;
#else
  case DAO_ARRAY  :  return & baseTyper;
#endif
                     /*     case DAO_REGEX    :  return & regexTyper; // XXX */
  case DAO_FUNCURRY : return & curryTyper;
  case DAO_CDATA   :  return & cdataTyper;
  case DAO_ROUTINE   :  return & routTyper;
  case DAO_FUNCTION   :  return & funcTyper;
  case DAO_CLASS     :  return & classTyper;
  case DAO_OBJECT    :  return & objTyper;
  case DAO_STREAM    :  return & streamTyper;
  case DAO_NAMESPACE :  return & nsTyper;
  case DAO_CONTEXT   :  return & ctxTyper;
  case DAO_VMPROCESS :  return & vmpTyper;
  case DAO_VMSPACE   :  return & vmsTyper;
  case DAO_ABSTYPE   :  return & abstypeTyper;
#ifdef DAO_WITH_MACRO
  case DAO_MACRO     :  return & macroTyper;
#endif
#ifdef DAO_WITH_THREAD
  case DAO_MUTEX     :  return & mutexTyper;
  case DAO_CONDVAR   :  return & condvTyper;
  case DAO_SEMA      :  return & semaTyper;
  case DAO_THREAD    :  return & threadTyper;
  case DAO_THDMASTER :  return & thdMasterTyper;
#endif
  default : break;
  }
  return & baseTyper;
}

void DaoVmSpace_TypeDef( DaoVmSpace *self, const char *old, const char *type )
{
  DaoAbsType *tp, *tp2;
  DString *name = DString_New(1);
  DString_SetMBS( name, old );
  if( self->nsWorking == NULL ) self->nsWorking = self->mainNamespace;
  tp = DaoNameSpace_FindAbsType( self->nsWorking, name );
  DString_SetMBS( name, type );
  tp2 = DaoNameSpace_FindAbsType( self->nsWorking, name );
  DString_Delete( name );
  if( tp == NULL ) return;
  if( tp2 == NULL ){
    tp = DaoAbsType_Copy( tp );
    DString_SetMBS( tp->name, type );
    DaoNameSpace_AddAbsType( self->nsWorking, tp->name, tp );
  }
}

void DaoVmSpace_SetOptions( DaoVmSpace *self, int options )
{
  self->options = options;
}
int DaoVmSpace_GetOptions( DaoVmSpace *self )
{
  return self->options;
}
DaoNameSpace* DaoVmSpace_MainNameSpace( DaoVmSpace *self )
{
  return self->mainNamespace;
}
DaoVmProcess* DaoVmSpace_MainVmProcess( DaoVmSpace *self )
{
  return self->mainProcess;
}
void DaoVmSpace_SetUserHandler( DaoVmSpace *self, DaoUserHandler *handler )
{
  self->userHandler = handler;
}
void DaoVmSpace_ReadLine( DaoVmSpace *self, ReadLine fptr )
{
  self->ReadLine = fptr;
}
void DaoVmSpace_AddHistory( DaoVmSpace *self, AddHistory fptr )
{
  self->AddHistory = fptr;
}
int DaoVmSpace_GetState( DaoVmSpace *self )
{
  return self->state;
}
void DaoVmSpace_Stop( DaoVmSpace *self, int bl )
{
  self->stopit = bl;
}

static DaoTypeBase vmsTyper=
{
  NULL,
  "VMSPACE",
  NULL,
  NULL,
  {0},
  (FuncPtrNew) DaoVmSpace_New,
  (FuncPtrDel) DaoVmSpace_Delete 
};


DaoVmSpace* DaoVmSpace_New()
{
  char *daodir = getenv( "DAO_DIR" );
  char pwd[512];
  DaoVmSpace *self = (DaoVmSpace*) dao_malloc( sizeof(DaoVmSpace) );
  DaoBase_Init( self, DAO_VMSPACE );
  getcwd( pwd, 511 );
  self->stdStream = DaoStream_New();
  self->stdStream->vmSpace = self;
  self->source = DString_New(1);
  self->options = 0;
  self->state = 0;
  self->feMask = 0;
  self->stopit = 0;
  self->safeTag = 1;
  self->userHandler = NULL;
  self->vfiles = DMap_New(D_STRING,D_STRING);
  self->nsModules = DMap_New(D_STRING,0);
  self->modRequire = DMap_New(D_STRING,0);
  self->allTokens = DMap_New(D_STRING,0);
  self->srcFName = DString_New(1);
  self->pathWorking = DString_New(1);
  self->nameLoading = DArray_New(D_STRING);
  self->pathLoading = DArray_New(D_STRING);
  self->pathSearching = DArray_New(D_STRING);

  if( daoConfig.safe ) self->options |= DAO_EXEC_SAFE;      

  self->argParams = DaoList_New();

  self->thdMaster = NULL;
#ifdef DAO_WITH_THREAD
  self->thdMaster = DaoThdMaster_New();
  self->thdMaster->refCount ++;
  DMutex_Init( & self->mutexLoad );
  self->locked = 0;
#endif

  self->nsWorking = NULL;
  self->nsInternal = NULL;
  self->nsInternal = DaoNameSpace_New( self );
  self->nsException = DaoNameSpace_New( self );
  self->mainNamespace = DaoNameSpace_New( self );
  self->nsInternal->vmSpace = self;
  self->nsException->vmSpace = self;
  self->mainNamespace->vmSpace = self;

  self->stdStream->refCount ++;
  self->nsInternal->refCount ++;
  self->nsException->refCount ++;
  self->mainNamespace->refCount ++;

  DString_SetMBS( self->mainNamespace->name, "MainNameSpace" );

  self->friendPids = DMap_New(D_STRING,0);
  self->pluginTypers = DArray_New(0);

  self->ReadLine = NULL;
  self->AddHistory = NULL;

  DaoVmSpace_SetPath( self, pwd );
  DaoVmSpace_AddPath( self, pwd );
  DaoVmSpace_AddPath( self, DAO_PATH );
  DaoVmSpace_AddPath( self, "~/dao" );
  if( daodir ) DaoVmSpace_AddPath( self, daodir );

  self->mainProcess = DaoVmProcess_New( self );
  self->mainProcess->mpiData = DaoMpiData_New();
  GC_IncRC( self->mainProcess );
  DString_SetMBS( self->mainProcess->mpiData->name, "main" );
  MAP_Insert( self->friendPids, self->mainProcess->mpiData->name, self->mainProcess );

  if( mainVmSpace ) DaoNameSpace_Import( self->nsInternal, mainVmSpace->nsInternal, 0 );
  DString_Clear( self->source );

  return self;
}
void DaoVmSpace_Delete( DaoVmSpace *self )
{
  DNode *node = DMap_First( self->nsModules );
#ifdef DEBUG 
  ObjectProfile[ DAO_VMSPACE ] --;
#endif
  for( ; node!=NULL; node = DMap_Next( self->nsModules, node ) ){
#if 0
    printf( "%i  %i  %s\n", node->value.pBase->refCount, 
    ((DaoNameSpace*)node->value.pBase)->cmethods->size, node->key.pString->mbs );
#endif
    GC_DecRC( node->value.pBase );
  }
  GC_DecRC( self->nsWorking );
  GC_DecRC( self->stdStream );
  GC_DecRC( self->thdMaster );
  DString_Delete( self->source );
  DString_Delete( self->srcFName );
  DString_Delete( self->pathWorking );
  DArray_Delete( self->nameLoading );
  DArray_Delete( self->pathLoading );
  DArray_Delete( self->pathSearching );
  DArray_Delete( self->pluginTypers );
  DMap_Delete( self->vfiles );
  DMap_Delete( self->allTokens );
  DMap_Delete( self->nsModules );
  DMap_Delete( self->modRequire );
  DMap_Delete( self->friendPids );
  DaoList_Delete( self->argParams );
  GC_DecRC( self->mainProcess );
  dao_free( self );
}
void DaoVmSpace_Lock( DaoVmSpace *self )
{
#ifdef DAO_WITH_THREAD
  if( self->locked ) return;
  DMutex_Lock( & self->mutexLoad );
  self->locked = 1;
#endif
}
void DaoVmSpace_Unlock( DaoVmSpace *self )
{
#ifdef DAO_WITH_THREAD
  if( self->locked ==0 ) return;
  self->locked = 0;
  DMutex_Unlock( & self->mutexLoad );
#endif
}
static int DaoVmSpace_ReadSource( DaoVmSpace *self, DString *fname )
{
  FILE *fin;
  int ch;
  DNode *node = MAP_Find( self->vfiles, fname );
  /* printf( "reading %s\n", fname->mbs ); */
  if( node ){
    DString_Assign( self->source, node->value.pString );
    return 1;
  }
  fin = fopen( fname->mbs, "r" );
  DString_Clear( self->source );
  if( ! fin ){
    DaoStream_WriteMBS( self->stdStream, "ERROR: can not open file \"" );
    DaoStream_WriteMBS( self->stdStream, fname->mbs );
    DaoStream_WriteMBS( self->stdStream, "\".\n" );
    return 0;
  }
  while( ( ch=getc(fin) ) != EOF ) DString_AppendChar( self->source, ch );
  fclose( fin );
  return 1;
}
void SplitByWhiteSpaces( DString *str, DArray *tokens )
{
  size_t i, size = str->size;
  const char *chs;
  DString *tok = DString_New(1);
  DArray_Clear( tokens );
  DString_ToMBS( str );
  chs = str->mbs;
  for( i=0; i<size; i++){
    if( chs[i] == '\\' && i+1 < size ){
      DString_AppendChar( tok, chs[i+1] );
      ++i;
      continue;
    }else if( isspace( chs[i] ) ){
      if( tok->size > 0 ){
        DArray_Append( tokens, tok );
        DString_Clear( tok );
      }
      continue;
    }
    DString_AppendChar( tok, chs[i] );
  }
  if( tok->size > 0 ) DArray_Append( tokens, tok );
  DString_Delete( tok );
}
static void ParseScriptParameters( DString *str, DArray *tokens )
{
  size_t i, size = str->size;
  char quote = 0;
  const char *chs;
  DString *tok = DString_New(1);
  /* The shell may remove quotation marks, to use an arbitrary string as
   * a parameter, the string should be enclosed insize \" \" with blackslashes.
   * If the string contains ", it should be preceded by 3 blackslashes as,
   * dao myscript.dao -m=\"just a message \\\"hello\\\".\"
   */
  /*
   printf( "2: %s\n", str->mbs );
   */

  DArray_Clear( tokens );
  DString_ToMBS( str );
  chs = str->mbs;
  for( i=0; i<size; i++){
    if( chs[i] == '\'' || chs[i] == '"' ) quote = chs[i];
    if( quote ){
      if( tok->size > 0 ){
        DArray_Append( tokens, tok );
        DString_Clear( tok );
      }
      i ++;
      while( i<size && chs[i] != quote ){
        if( chs[i] == '\\' ){
          DString_AppendChar( tok, chs[++i] );
          i ++;
          continue;
        }
        DString_AppendChar( tok, chs[i++] );
      }
      DArray_Append( tokens, tok );
      DString_Clear( tok );
      quote = 0;
      continue;
    }else if( tokens->size && chs[i] == '\\' && i+1 < size ){
      /* Do NOT remove backslashes for script path:
        dao C:\test\hello.dao ... */
      DString_AppendChar( tok, chs[i+1] );
      ++i;
      continue;
    }else if( isspace( chs[i] ) || chs[i] == '=' ){
      if( tok->size > 0 ){
        DArray_Append( tokens, tok );
        DString_Clear( tok );
      }
      if( chs[i] == '=' ){
        DString_AppendChar( tok, chs[i] );
        DArray_Append( tokens, tok );
        DString_Clear( tok );
      }
      continue;
    }
    DString_AppendChar( tok, chs[i] );
  }
  if( tok->size > 0 ) DArray_Append( tokens, tok );
  DString_Delete( tok );
}

int DaoVmSpace_ParseOptions( DaoVmSpace *self, const char *options )
{
  DString *str = DString_New(1);
  DArray *array = DArray_New(D_STRING);
  size_t i, j;

  DString_SetMBS( str, options );
  SplitByWhiteSpaces( str, array );
  for( i=0; i<array->size; i++ ){
    DString *token = array->items.pString[i];
    if( token->mbs[0] =='-' && token->size >1 && token->mbs[1] =='-' ){
      if( strcmp( token->mbs, "--help" ) ==0 ){
        self->options |= DAO_EXEC_HELP;
      }else if( strcmp( token->mbs, "--version" ) ==0 ){
        self->options |= DAO_EXEC_VINFO;
      }else if( strcmp( token->mbs, "--debug" ) ==0 ){
        self->options |= DAO_EXEC_DEBUG;
      }else if( strcmp( token->mbs, "--safe" ) ==0 ){
        self->options |= DAO_EXEC_SAFE;
        daoConfig.safe = 1;
      }else if( strcmp( token->mbs, "--interactive" ) ==0 ){
        self->options |= DAO_EXEC_INTERUN;
      }else if( strcmp( token->mbs, "--list-bc" ) ==0 ){
        self->options |= DAO_EXEC_LIST_BC;
      }else if( strcmp( token->mbs, "--list-bc" ) ==0 ){
        self->options |= DAO_EXEC_LIST_BC;
      }else if( strcmp( token->mbs, "--compile" ) ==0 ){
        self->options |= DAO_EXEC_COMP_BC;
      }else if( strcmp( token->mbs, "--incr-comp" ) ==0 ){
        self->options |= DAO_EXEC_INCR_COMP;
        daoConfig.incompile = 1;
      }else if( strcmp( token->mbs, "--no-typed-code" ) ==0 ){
        self->options |= DAO_EXEC_NO_TC;
        daoConfig.typedcode = 0;
      }else if( strcmp( token->mbs, "--no-typedcode" ) ==0 ){
        self->options |= DAO_EXEC_NO_JIT;
        daoConfig.jit = 0;
      }else if( strcmp( token->mbs, "--proc-name" ) ==0 ){
        putenv( "PROC_NAME=MASTER" );
      }else if( strncmp( token->mbs, "--proc-name=", 12 ) ==0 ){
        static char buf[256] = "PROC_NAME=";
        strncat( buf, token->mbs+12, 240 );
        putenv( buf );
      }else if( strcmp( token->mbs, "--proc-port" ) ==0 ){
        putenv( "PROC_PORT=4115" );
      }else if( strncmp( token->mbs, "--proc-port=", 12 ) ==0 ){
        static char buf[256] = "PROC_PORT=";
        strncat( buf, token->mbs+12, 240 );
        putenv( buf );
      }else{
        DaoStream_WriteMBS( self->stdStream, "Unknown option: " );
        DaoStream_WriteMBS( self->stdStream, token->mbs );
        DaoStream_WriteMBS( self->stdStream, ";\n" );
      }
    }else if( DString_MatchMBS( token, " ^ [%C_]+=.* ", NULL, NULL ) ){
      token = DString_DeepCopy( token );
      putenv( token->mbs );
    }else{
      size_t len = token->size;
      DString_Clear( str );
      for( j=0; j<len; j++ ){
        switch( token->mbs[j] ){
        case 'h' : self->options |= DAO_EXEC_HELP;      break;
        case 'v' : self->options |= DAO_EXEC_VINFO;     break;
        case 'd' : self->options |= DAO_EXEC_DEBUG;     break;
        case 'i' : self->options |= DAO_EXEC_INTERUN;   break;
        case 'l' : self->options |= DAO_EXEC_LIST_BC;   break;
        case 'c' : self->options |= DAO_EXEC_COMP_BC;   break;
        case 's' : self->options |= DAO_EXEC_SAFE;      
                   daoConfig.safe = 1;
                   break;
        case 'n' : self->options |= DAO_EXEC_INCR_COMP;
                   daoConfig.incompile = 0;
                   break;
        case 'T' : self->options |= DAO_EXEC_NO_TC; 
                   daoConfig.typedcode = 0;
                   break;
        case 'J' : self->options |= DAO_EXEC_NO_JIT; 
                   daoConfig.jit = 0;
                   break;
        case 'p' : {
                     static char buf[256] = "PROC_NAME=";
                     if( j == token->size-1 )
                       strncat( buf, "MASTER", 240 );
                     else
                       strncat( buf, token->mbs+j+1, 240 );
                     putenv( buf );
                     j = len;
                     break;
                   }
        case '-' : break;
        default :
                   DString_AppendChar( str, token->mbs[j] );
                   DString_AppendChar( str, ' ' );
                   break;
        }
      }
      if( str->size > 0 ){
        DaoStream_WriteMBS( self->stdStream, "Unknown option: " );
        DaoStream_WriteMBS( self->stdStream, str->mbs );
        DaoStream_WriteMBS( self->stdStream, ";\n" );
      }
    }
  }
#if( defined DAO_WITH_NETWORK && defined DAO_WITH_MPI )
  if( getenv( "PROC_NAME" ) || getenv( "PROC_PORT" ) ) DaoProxy_Init( self );
#endif
  DString_Delete( str );
  DArray_Delete( array );
  return 1;
}
extern int IsValidName( const char *chs );
extern int IsNumber( const char *chs );

static DValue DaoParseNumber( const char *s )
{
  DValue value = daoNilValue;
  if( strchr( s, 'e' ) != NULL ){
    value.t = DAO_FLOAT;
    value.v.f = strtod( s, 0 );
  }else if( strchr( s, 'E' ) != NULL ){
    value.t = DAO_DOUBLE;
    value.v.d = strtod( s, 0 );
  }else if( strchr( s, '.' ) != NULL ){
    int len = strlen( s );
    if( strstr( s, "00" ) == s + (len-2) ){
      value.t = DAO_DOUBLE;
      value.v.d = strtod( s, 0 );
    }else{
      value.t = DAO_FLOAT;
      value.v.f = strtod( s, 0 );
    }
  }else{
    value.t = DAO_INTEGER;
    value.v.i = strtod( s, 0 );
  }
  return value;
}

static int DaoVmSpace_CompleteModuleName( DaoVmSpace *self, DString *fname );

static DaoNameSpace* 
DaoVmSpace_LoadDaoByteCode( DaoVmSpace *self, DString *fname, int run );

static DaoNameSpace* 
DaoVmSpace_LoadDaoAssembly( DaoVmSpace *self, DString *fname, int run );

static DaoNameSpace* 
DaoVmSpace_LoadDaoModuleExt( DaoVmSpace *self, DString *libpath, int force, int run );

int proxy_started = 0;

static void DaoVmSpace_ParseArguments( DaoVmSpace *self, const char *file,
    DArray *argNames, DArray *argValues )
{
  DaoAbsType *nested[2];
  DaoNameSpace *ns = self->nsInternal;
  DaoList *argv = DaoList_New();
  DaoMap *cmdarg = DaoMap_New(0);
  DArray *array = DArray_New(D_STRING);
  DString *str = DString_New(1);
  DString *key = DString_New(1);
  DString *val = DString_New(1);
  DValue nkey = daoZeroInt;
  DValue skey = daoNilString;
  DValue sval = daoNilString;
  size_t i, pk;
  skey.v.s = key;
  sval.v.s = val;
  nested[0] = DaoNameSpace_MakeAbsType( ns, "any", DAO_ANY, NULL,NULL,0 );
  nested[1] = DaoNameSpace_MakeAbsType( ns, "string",DAO_STRING, NULL,NULL,0 );
  cmdarg->unitype = DaoNameSpace_MakeAbsType( ns, "map",DAO_MAP,NULL,nested,2);
  argv->unitype = DaoNameSpace_MakeAbsType( ns, "list",DAO_LIST,NULL,nested+1,1);
  GC_IncRC( cmdarg->unitype );
  GC_IncRC( argv->unitype );
  DString_SetMBS( str, file );
  ParseScriptParameters( str, array );
  DString_Assign( self->srcFName, array->items.pString[0] );
  DString_Assign( val, array->items.pString[0] );
  DaoList_Append( argv, sval );
  DaoMap_Insert( cmdarg, nkey, sval );
  DaoVmSpace_MakePath( self, self->srcFName, 1 );
  i = 1;
  while( i < array->size ){
    DString *s = array->items.pString[i];
    DString *name = NULL, *value = NULL;
    i ++;
    if( s->mbs[0] == '-' ){
      name = s;
      if( i < array->size ){
        /* -name[=]value */
        s = array->items.pString[i];
        if( s->mbs[0] == '=' && s->mbs[1] == 0 ) i++;
        if( i < array->size ){
          s = array->items.pString[i];
          if( s->mbs[0] != '-' || IsNumber( s->mbs+1 ) ){
            value = s;
            i ++;
            if( i < array->size ){
              s = array->items.pString[i];
              if( s->mbs[0] == '=' && s->mbs[1] == 0 ){
                value = NULL;
                i --;
              }
            }
          }
        }
      }
    }else if( IsValidName( s->mbs ) && i < array->size ){
      /* name=value */
      if( STRCMP( array->items.pString[i], "=" ) ==0 ){
        name = s;
        i ++;
        if( i < array->size ){
          s = array->items.pString[i];
          if( s->mbs[0] != '-' || IsNumber( s->mbs+1 ) ){
            value = s;
            i ++;
          }
        }
      }else{
        value = s;
      }
    }else if( STRCMP( s, "=" ) !=0 ){
      value = s;
    }
    /*
    printf( "%i: %s, %s\n", i, name?name->mbs:"", value?value->mbs:"" );
    */
    if( name ){
      pk = 0;
      while( name->mbs[pk]=='-' ) pk ++;
      DString_Substr( name, str, pk, -1 );
      if( value == NULL ){
        value = name;
        while( value->mbs[0] == '-' ) DString_Erase( value, 0, 1 );
      }
      DArray_Append( argNames, str );
      DArray_Append( argValues, value );
    }else if( value ){
      DString_Clear( str );
      DArray_Append( argNames, str );
      DArray_Append( argValues, value );
      name = value;
    }else{
      continue;
    }

    nkey.v.i ++;
    DString_Assign( val, value );
    DaoList_Append( argv, sval );
    DaoMap_Insert( cmdarg, nkey, sval );
    pk = 0;
    while( name->mbs[pk]=='-' ) pk ++;
    DString_Substr( name, key, pk, -1 );
    DaoMap_Insert( cmdarg, skey, sval );
  }
  DString_SetMBS( str, "ARGV" );
  nkey.t = DAO_LIST;
  nkey.v.list = argv;
  DaoNameSpace_AddConst( self->nsInternal, str, nkey );
  DString_SetMBS( str, "CMDARG" );
  nkey.t = DAO_MAP;
  nkey.v.map = cmdarg;
  DaoNameSpace_AddConst( self->nsInternal, str, nkey );
  DArray_Delete( array );
  DString_Delete( key );
  DString_Delete( val );
  DString_Delete( str );
}
static void DaoVmSpace_ConvertArguments( DaoVmSpace *self, DaoNameSpace *ns,
    DArray *argNames, DArray *argValues )
{
  DaoRoutine *rout = ns->mainRoutine;
  DaoAbsType *abtp = rout->routType;
  DString *key = DString_New(1);
  DString *val = DString_New(1);
  DString *str;
  DValue nkey = daoZeroInt;
  DValue skey = daoNilString;
  DValue sval = daoNilString;
  int i;
  skey.v.s = key;
  sval.v.s = val;
  DaoList_Clear( self->argParams );
  DString_SetMBS( key, "main" );
  i = ns ? DaoNameSpace_FindConst( ns, key ) : -1;
  skey.sub = DAO_PARNAME;
  if( i >=0 ){
    DValue nkey = DaoNameSpace_GetConst( ns, i );
    /* It may has not been compiled if it is not called explicitly. */
    if( nkey.t == DAO_ROUTINE ){
      DaoRoutine_Compile( nkey.v.routine );
      rout = nkey.v.routine;
      abtp = rout->routType;
    }
  }
  if( rout == NULL ){
    DString_Delete( key );
    DString_Delete( val );
    return;
  }
  for( i=0; i<argNames->size; i++ ){
    nkey = sval;
    /*
      printf( "argname = %s; argval = %s\n", argNames->items.pString[i]->mbs,
      argValues->items.pString[i]->mbs );
      */
    DString_Assign( val, argValues->items.pString[i] );
    if( abtp->nested->size > i ){
      if( abtp->nested->items.pBase[i] ){
        int k = abtp->nested->items.pAbtp[i]->tid;
        char *chars = argValues->items.pString[i]->mbs;
        if( k == DAO_PAR_NAMED || k == DAO_PAR_DEFAULT )
          k = abtp->nested->items.pAbtp[i]->X.abtype->tid;
        if( chars[0] == '+' || chars[0] == '-' ) chars ++;
        str = argNames->items.pString[i];
        if( str->size && abtp->mapNames ){
          DNode *node = MAP_Find( abtp->mapNames, str );
          if( node ){
            int id = node->value.pInt & MAPF_MASK;
            k = abtp->nested->items.pAbtp[id]->tid;
            if( k == DAO_PAR_NAMED || k == DAO_PAR_DEFAULT )
              k = abtp->nested->items.pAbtp[id]->X.abtype->tid;
          }
        }
        if( k >0 && k <= DAO_DOUBLE && IsNumber( chars ) ){
          nkey = DaoParseNumber( chars );
        }
      }
    }
    if( argNames->items.pString[i]->size ){
      DValue vp = daoNilPair;
      DString_Assign( key, argNames->items.pString[i] );
      vp.v.pair = DaoPair_New( skey, nkey );
      vp.v.pair->subType |= DAO_DATA_CONST;
      vp.v.pair->type = vp.t = DAO_PAR_NAMED;
      DaoList_Append( self->argParams, vp );
    }else{
      DaoList_Append( self->argParams, nkey );
    }
  }
  DString_Delete( key );
  DString_Delete( val );
}

DaoNameSpace* DaoVmSpace_Load( DaoVmSpace *self, const char *file )
{
  DArray *argNames = DArray_New(D_STRING);
  DArray *argValues = DArray_New(D_STRING);
  DaoNameSpace *ns = NULL;
  int mtp;
  DaoVmSpace_ParseArguments( self, file, argNames, argValues );
  mtp = DaoVmSpace_CompleteModuleName( self, self->srcFName );
  switch( mtp ){
  case DAO_MODULE_DAO_O :
    ns = DaoVmSpace_LoadDaoByteCode( self, self->srcFName, 0 );
    break;
  case DAO_MODULE_DAO_S :
    ns = DaoVmSpace_LoadDaoAssembly( self, self->srcFName, 0 );
    break;
  case DAO_MODULE_DAO :
    ns = DaoVmSpace_LoadDaoModuleExt( self, self->srcFName, 0, 0 );
    break;
  default :
    /* also allows execution of script files without suffix .dao */
    ns = DaoVmSpace_LoadDaoModuleExt( self, self->srcFName, 0, 0 );
    break;
  }
  if( ns ) DaoVmSpace_ConvertArguments( self, ns, argNames, argValues );
  DArray_Delete( argNames );
  DArray_Delete( argValues );
  if( ns == NULL ) return 0;

#if 0
  //  if( self->nsWorking != self->nsConsole ) /* not in interactive mode */
  //    DaoNameSpace_Import( self->nsWorking, ns, NULL );
#endif

#ifdef DAO_WITH_NETWORK
#if 0
  /* XXX, see comments near daoProxyScripts */
  if( ! proxy_started && ( getenv( "PROC_NAME" ) || getenv( "PROC_PORT" ) ) ){
    /* start Proxy_Receiver() after compiling the script file from command line; */
    proxy_started = 0;
    DString_SetMBS( self->source, daoProxyScripts );
    DaoVmSpace_Compile( self, self->source, 1, NULL );
    DaoVmSpace_RunMain( self, self->nsWorking );
  }
#endif
#endif
  return ns;
}

static void DaoVmSpace_Interun( DaoVmSpace *self, CallbackOnString callback )
{
  DString *input = DString_New(1);
  const char *varRegex = "^ %s* %w+ %s* $";
  const char *srcRegex = "^ %s* %w+ %. dao .* $";
  const char *sysRegex = "^ %\\ %s* %w+ %s* .* $";
  char *chs;
  int ch;
  self->options &= ~DAO_EXEC_INTERUN;
  self->state |= DAO_EXEC_INTERUN;
  while(1){
    DString_SetMBS( self->srcFName, "" );
    DString_Clear( input );
    if( self->ReadLine ){
      chs = self->ReadLine( "(dao) " );
      if( chs ){
        DString_SetMBS( input, chs );
        DString_Simplify( input );
        if( input->size && self->AddHistory ) self->AddHistory( chs );
        dao_free( chs );
      }
    }else{
      printf( "(dao) " );
      ch = getchar();
      while( ch != '\n' && ch != EOF ){
        DString_AppendChar( input, (char)ch );
        ch = getchar();
      }
      DString_Simplify( input );
    }
    if( input->size == 0 ) continue;
    self->stopit = 0;
    if( strcmp( input->mbs, "q" ) == 0 ){
      break;
    }else if( DString_MatchMBS( input, sysRegex, NULL, NULL ) ){
      if( system( input->mbs+1 ) ==-1) printf( "shell command failed\n" );
    }else if( DString_MatchMBS( input, srcRegex, NULL, NULL ) ){
      DString_InsertMBS( input, "stdlib.load(", 0, 0 );
      DString_AppendMBS( input, ")" );
      if( callback ){
        (*callback)( input->mbs );
        continue;
      }
      DaoVmProcess_Eval( self->mainProcess, self->mainNamespace, input, 1 );
    }else if( DString_MatchMBS( input, varRegex, NULL, NULL ) ){
      DString_InsertMBS( input, "stdio.println(", 0, 0 );
      DString_AppendMBS( input, ")" );
      if( callback ){
        (*callback)( input->mbs );
        continue;
      }
      DaoVmProcess_Eval( self->mainProcess, self->mainNamespace, input, 1 );
    }else{
      if( callback ){
        (*callback)( input->mbs );
        continue;
      }
      DaoVmProcess_Eval( self->mainProcess, self->mainNamespace, input, 1 );
    }
    /*
    printf( "%s\n", input->mbs );
    */
  }
  DString_Delete( input );
}

static void DaoVmSpace_ExeCmdArgs( DaoVmSpace *self )
{
  DaoNameSpace *ns = self->mainNamespace;
  DaoRoutine *rout;
  size_t i, j, n;
  if( self->options & DAO_EXEC_VINFO ) DaoStream_WriteMBS( self->stdStream, copy_notice );
  if( self->options & DAO_EXEC_HELP )  DaoStream_WriteMBS( self->stdStream, cmd_help );

  if( self->options & DAO_EXEC_LIST_BC ){
    for( i=ns->cstUser; i<ns->cstData->size; i++){
      DValue p = ns->cstData->data[i];
      if( p.t == DAO_ROUTINE && p.v.routine != ns->mainRoutine ){
        n = p.v.routine->routOverLoad->size;
        for(j=0; j<n; j++){
          rout = (DaoRoutine*) p.v.routine->routOverLoad->items.pBase[j];
          DaoRoutine_Compile( rout );
          DaoRoutine_PrintCode( rout, self->stdStream );
        }
      }else if( p.t == DAO_CLASS ){
        DaoClass_PrintCode( p.v.klass, self->stdStream );
      }
    }
    if( ns->mainRoutine )
      DaoRoutine_PrintCode( ns->mainRoutine, self->stdStream );
    if( ( self->options & DAO_EXEC_INTERUN ) && self->userHandler == NULL )
      DaoVmSpace_Interun( self, NULL );
  }
}
int DaoVmSpace_RunMain( DaoVmSpace *self, const char *file )
{
  DaoNameSpace *ns = self->mainNamespace;
  DaoVmProcess *vmp = self->mainProcess;
  DaoContext *ctx = NULL;
  DaoRoutine *mainRoutine;
  DString *name;
  DArray *array;
  DArray *argNames;
  DArray *argValues;
  DValue value;
  DValue *ps;
  size_t N;
  int i, j, res;

  if( file == NULL || strlen(file) ==0 ){
    DArray_PushFront( self->nameLoading, self->pathWorking );
    DArray_PushFront( self->pathLoading, self->pathWorking );
    DaoVmSpace_ExeCmdArgs( self );
    if( ( self->options & DAO_EXEC_INTERUN ) && self->userHandler == NULL )
      DaoVmSpace_Interun( self, NULL );
    return 1;
  }
  argNames = DArray_New(D_STRING);
  argValues = DArray_New(D_STRING);
  DaoVmSpace_ParseArguments( self, file, argNames, argValues );
  DaoNameSpace_SetName( ns, self->srcFName->mbs );
  DaoVmSpace_AddPath( self, ns->path->mbs );
  DArray_PushFront( self->nameLoading, ns->name );
  DArray_PushFront( self->pathLoading, ns->path );
  MAP_Insert( self->nsModules, ns->name, ns );

  /* self->srcFName may has been changed */
  res = DaoVmSpace_ReadSource( self, ns->name );
  res = res && DaoVmProcess_Compile( vmp, ns, self->source, 1 );
  if( res ) DaoVmSpace_ConvertArguments( self, ns, argNames, argValues );
  DArray_Delete( argNames );
  DArray_Delete( argValues );

  if( res == 0 ) return 0;

  name = DString_New(1);
  mainRoutine = ns->mainRoutine;
  DString_SetMBS( name, "main" );
  i = DaoNameSpace_FindConst( ns, name );
  DString_Delete( name );
  
  ps = self->argParams->items->data;
  N = self->argParams->items->size;
  array = DArray_New(0);
  DArray_Resize( array, N, NULL );
  for(j=0; j<N; j++) array->items.pValue[j] = ps + j;
  if( i >=0 ){
    value = DaoNameSpace_GetConst( ns, i );
    if( value.t == DAO_ROUTINE ){
      mainRoutine = value.v.routine;
      ctx = DaoVmProcess_MakeContext( vmp, mainRoutine );
      ctx->vmSpace = self;
      DaoContext_Init( ctx, mainRoutine );
      if( DaoContext_InitWithParams( ctx, vmp, array->items.pValue, N ) == 0 ){
        DaoStream_WriteMBS( self->stdStream, "ERROR: invalid command line arguments.\n" );
        DaoStream_WriteString( self->stdStream, mainRoutine->docString );
        DArray_Delete( array );
        return 0;
      }
      DaoVmProcess_PushContext( vmp, ctx );
    }
    mainRoutine = ns->mainRoutine;
  }
  DaoVmSpace_ExeCmdArgs( self );
  /* always execute default ::main() routine first for initialization: */
  if( mainRoutine ){
    DaoVmProcess_PushRoutine( vmp, mainRoutine );
    DaoVmProcess_Execute( vmp );
  }
  /* check and execute explicitly defined main() routine  */
  if( i >=0 ){
    value = DaoNameSpace_GetConst( ns, i );
    if( value.t == DAO_ROUTINE ){
      if( ! DRoutine_PassParams( (DRoutine*)ctx->routine, NULL, ctx->regValues, 
            array->items.pValue, NULL, N, 0 ) ){
        DaoStream_WriteMBS( self->stdStream, "ERROR: invalid command line arguments.\n" );
        DaoStream_WriteString( self->stdStream, ctx->routine->docString );
        DaoVmProcess_CacheContext( vmp, ctx );
        DArray_Delete( array );
        return 0;
      }
      DaoVmProcess_Execute( vmp );
    }
  }
  DArray_Delete( array );
  if( ( self->options & DAO_EXEC_INTERUN ) && self->userHandler == NULL )
    DaoVmSpace_Interun( self, NULL );

  return 1;
}
static int DaoVmSpace_CompleteModuleName( DaoVmSpace *self, DString *fname )
{
  int slen = strlen( DAO_DLL_SUFFIX );
  int i, size, modtype = DAO_MODULE_NONE;
  DString_ToMBS( fname );
  size = fname->size;
  if( size >6 && DString_FindMBS( fname, ".dao.o", 0 ) == size-6 ){
    DaoVmSpace_MakePath( self, fname, 1 );
    if( TestPath( self, fname ) ) modtype = DAO_MODULE_DAO_O;
  }else if( size >6 && DString_FindMBS( fname, ".dao.s", 0 ) == size-6 ){
    DaoVmSpace_MakePath( self, fname, 1 );
    if( TestPath( self, fname ) ) modtype = DAO_MODULE_DAO_S;
  }else if( size >4 && ( DString_FindMBS( fname, ".dao", 0 ) == size-4
        || DString_FindMBS( fname, ".cgi", 0 ) == size-4 ) ){
    DaoVmSpace_MakePath( self, fname, 1 );
    if( TestPath( self, fname ) ) modtype = DAO_MODULE_DAO;
  }else if( size > slen && DString_FindMBS( fname, DAO_DLL_SUFFIX, 0 ) == size - slen ){
    modtype = DAO_MODULE_DLL;
#ifdef UNIX
    if( DString_FindMBS( fname, ".dll", 0 ) == size-4 )
      DString_Erase( fname, size-4, 4 );
#ifdef MAC_OSX
    if( DString_FindMBS( fname, ".dylib", 0 ) != size-6 )
      DString_AppendMBS( fname, ".dylib" );
#else
    if( DString_FindMBS( fname, ".so", 0 ) != size-3 )
      DString_AppendMBS( fname, ".so" );
#endif
#elif WIN32
    if( DString_FindMBS( fname, ".so", 0 ) == size-3 )
      DString_Erase( fname, size-3, 3 );
    if( DString_FindMBS( fname, ".dll", 0 ) != size-4 )
      DString_AppendMBS( fname, ".dll" );
#endif
    DaoVmSpace_MakePath( self, fname, 1 );
    if( TestPath( self, fname ) ) modtype = DAO_MODULE_DLL;
  }else{
    DString *fn = DString_New(1);
    for(i=0; i<4; i++){
      DString_Assign( fn, fname );
      DString_AppendMBS( fn, daoFileSuffix[i] );
      DaoVmSpace_MakePath( self, fn, 1 );
#if 0
      printf( "%s %s\n", fn->mbs, self->nameLoading->items.pString[0]->mbs );
#endif
      /* skip the current file: reason, example, in gsl_vector.dao:
           load gsl_vector require gsl_complex, gsl_block;
         which will allow searching for gsl_vector.so, gsl_vector.dylib or gsl_vector.dll. */
      if( DString_EQ( fn, self->nameLoading->items.pString[0] ) ) continue;
      if( TestPath( self, fn ) ){
        modtype = i+1;
        DString_Assign( fname, fn );
        break;
      }
    }
    DString_Delete( fn );
  }
  return modtype;
}
#ifdef DAO_WITH_ASMBC
static DaoNameSpace* DaoVmSpace_LoadDaoByteCode( DaoVmSpace *self, DString *fname, int run )
{
  DString *asmc = DString_New(1); /*XXX*/
  DaoNameSpace *ns;
  DNode *node;
  node = MAP_Find( self->nsModules, fname );
  if( node ) return (DaoNameSpace*) node->value.pBase;
  if( ! DaoVmSpace_ReadSource( self, fname ) ) return 0;
  ns = DaoNameSpace_New( self );
  if( DaoParseByteCode( self, ns, self->source, asmc ) ){
    if( run ){
      DaoVmProcess *vmProc = DaoVmProcess_New( self );
      GC_IncRC( vmProc );
      DaoVmProcess_PushRoutine( vmProc, ns->mainRoutine );
      if( ! DaoVmProcess_Execute( vmProc ) ){
        GC_DecRC( vmProc );
        return 0;
      }
      GC_DecRC( vmProc );
    }
    FILE *fout = fopen( "bytecodes.dao.s", "w" );
    int i = 0;
    for(i=0; i<asmc->size; i++) fprintf( fout, "%c", asmc->mbs[i] );
    fclose( fout );
    return ns;
  }
  DaoNameSpace_Delete( ns );
  return NULL;
}
static DaoNameSpace* DaoVmSpace_LoadDaoAssembly( DaoVmSpace *self, DString *fname, int run )
{
  DString *bc = DString_New(1);
  DaoNameSpace *ns;
  DNode *node;
  node = MAP_Find( self->nsModules, fname );
  if( node ) return (DaoNameSpace*) node->value.pBase;
  if( ! DaoVmSpace_ReadSource( self, fname ) ) return 0;
  ns = DaoNameSpace_New( self );
  if( DaoParseAssembly( self, ns, self->source, bc ) ){
    if( run ){
      DaoVmProcess *vmProc = DaoVmProcess_New( self );
      GC_IncRC( vmProc );
      DaoVmProcess_PushRoutine( vmProc, ns->mainRoutine );
      if( ! DaoVmProcess_Execute( vmProc ) ){
        GC_DecRC( vmProc );
        return 0;
      }
      GC_DecRC( vmProc );
    }
    FILE *fout = fopen( "bytecodes.dao.o", "w" );
    int i = 0;
    for(i=0; i<bc->size; i++) fprintf( fout, "%c", bc->mbs[i] );
    fclose( fout );
    return ns;
  }
  DaoNameSpace_Delete( ns );
  return NULL;
}
#else
static DaoNameSpace* DaoVmSpace_LoadDaoByteCode( DaoVmSpace *self, DString *fname, int run )
{
  DaoStream_WriteMBS( self->stdStream, "ERROR: bytecode loader is disabled.\n" );
  return NULL;
}
static DaoNameSpace* DaoVmSpace_LoadDaoAssembly( DaoVmSpace *self, DString *fname, int run )
{
  DaoStream_WriteMBS( self->stdStream, "ERROR: assembly loader is disabled.\n" );
  return NULL;
}
#endif
static DaoNameSpace* DaoVmSpace_LoadDaoModuleExt( DaoVmSpace *self,
    DString *libpath, int force, int run )
{
  DaoParser *parser;
  DaoVmProcess *vmProc;
  DaoNameSpace *ns;
  DNode *node;
  ullong_t tm = 0;
  size_t i = DString_FindMBS( libpath, "/addpath.dao", 0 );
  size_t j = DString_FindMBS( libpath, "/delpath.dao", 0 );
  int bl;
  int cfgpath = i != MAXSIZE && i == libpath->size - 12;
  cfgpath = cfgpath || (j != MAXSIZE && j == libpath->size - 12);
  /*  XXX if cfgpath == true, only parsing? */

  DString_SetMBS( self->srcFName, libpath->mbs );
  if( ! DaoVmSpace_ReadSource( self, libpath ) ) return 0;

  /*
  printf("%p : loading %s\n", self, libpath->mbs );
  */
  parser = DaoParser_New();
  DString_Assign( parser->srcFName, self->srcFName );
  parser->vmSpace = self;
  parser->nameSpace = self->nsWorking;
  if( ! DaoParser_LexCode( parser, DString_GetMBS( self->source ), 1 ) ) goto LaodingFailed;

  node = MAP_Find( self->nsModules, libpath );
  tm = FileChangedTime( libpath->mbs );
  /* printf( "time = %lli, %s\n", tm, libpath->mbs ); */
  if( node && ! force ){
    ns = (DaoNameSpace*)node->value.pBase;
    if( ns->time >= tm ) goto ExecuteModule;
  }

  ns = DaoNameSpace_New( self );
  ns->time = tm;
  DString_Assign( ns->source, self->source );
#if 0
//  if( self->nsWorking != self->nsConsole ) /* not in interactive mode */
//XXX    DaoNameSpace_Import( ns, self->nsWorking, NULL );
#endif
  /*
  printf("%p : loading %s: %p %p\n", self, libpath->mbs, self->nsWorking, ns );
  */

  GC_IncRC( ns );
  node = MAP_Find( self->nsModules, libpath );
  if( node ) GC_DecRC( node->value.pBase );
  MAP_Insert( self->nsModules, libpath, ns );

#if 0
  tok = parser->tokStr->items.pString;
  for( i=0; i<parser->tokStr->size; i++){
    node = MAP_Find( self->allTokens, tok[i] );
    if( node ){
      DArray_Append( ns->tokStr, node->key.pString );
    }else{
      MAP_Insert( self->allTokens, tok[i], 1 );
      DArray_Append( ns->tokStr, tok[i] );
    }
  }
#endif

  /*
   printf("%p : parsing %s\n", self, libpath->mbs );
   */
  DString_Assign( ns->name, libpath );
  DString_Assign( ns->file, libpath );
  DArray_PushFront( self->nameLoading, libpath );
  i = DString_RFindChar( libpath, '/', -1 );
  if( i != MAXSIZE ){
    DString_Erase( ns->file, 0, i+1 );
    DArray_PushFront( self->pathLoading, libpath );
    DString_Erase( self->pathLoading->items.pString[0], i, -1 );
    DString_Assign( ns->path, self->pathLoading->items.pString[0] );
  }
  parser->nameSpace = ns;
  bl = DaoParser_ParseScript( parser );
  if( i != MAXSIZE ){
    DArray_PopFront( self->pathLoading );
  }
  DArray_PopFront( self->nameLoading );
  if( ! bl ) goto LaodingFailed;

  if( ns->mainRoutine == NULL ) goto LaodingFailed;
  DString_SetMBS( ns->mainRoutine->routName, "::main" );

ExecuteModule :
  DaoParser_Delete( parser );
  if( run && ns->mainRoutine->vmCodes->size > 1 ){
    vmProc = DaoVmProcess_New( self );
    GC_IncRC( vmProc );
    DArray_PushFront( self->nameLoading, ns->path );
    DArray_PushFront( self->pathLoading, ns->path );
    DaoVmProcess_PushRoutine( vmProc, ns->mainRoutine );
    if( ! DaoVmProcess_Execute( vmProc ) ){
      GC_DecRC( vmProc );
      DArray_PopFront( self->nameLoading );
      DArray_PopFront( self->pathLoading );
      return 0;
    }
    GC_DecRC( vmProc );
    DArray_PopFront( self->nameLoading );
    DArray_PopFront( self->pathLoading );
  }
  return ns;
LaodingFailed :
  DaoParser_Delete( parser );
  return 0;
}
DaoNameSpace* DaoVmSpace_LoadDaoModule( DaoVmSpace *self, DString *libpath )
{
  return DaoVmSpace_LoadDaoModuleExt( self, libpath, 0, 1 );
}

static void* DaoOpenDLL( const char *name );
static void* DaoGetSymbolAddress( void *handle, const char *name );
DAO_DLL void DaoInitAPI( DaoAPI *api );

DaoNameSpace* DaoVmSpace_LoadDllModule( DaoVmSpace *self, DString *libpath, DArray *reqns )
{
  DaoNameSpace *ns;
  DaoAPI *api;
  DNode *node;
  typedef void (*FuncType)( DaoVmSpace *, DaoNameSpace * );
  void (*funpter)( DaoVmSpace *, DaoNameSpace * );
  void *handle;
  long *dhv;
  int i;

  if( self->options & DAO_EXEC_SAFE ){
    DaoStream_WriteMBS( self->stdStream, 
        "ERROR: not permitted to open shared library in safe running mode.\n" );
    return NULL;
  }

  handle = DaoOpenDLL( libpath->mbs );
  if( ! handle ){
    DaoStream_WriteMBS( self->stdStream, "ERROR: unable to open the library file \"" );
    DaoStream_WriteMBS( self->stdStream, libpath->mbs );
    DaoStream_WriteMBS( self->stdStream, "\".\n");
    return 0;
  }
  
  node = MAP_Find( self->nsModules, libpath );
  if( node ){
    ns = (DaoNameSpace*) node->value.pBase;
    /* XXX dlclose(  ns->libHandle ) */
    if( handle == ns->libHandle ) return ns; 
  }else{
    ns = DaoNameSpace_New( self );
    GC_IncRC( ns );
    MAP_Insert( self->nsModules, libpath, ns );
    i = DString_RFindChar( libpath, '/', -1 );
    if( i != MAXSIZE ) DString_Erase( libpath, 0, i+1 );
    i = DString_RFindChar( libpath, '\\', -1 );
    if( i != MAXSIZE ) DString_Erase( libpath, 0, i+1 );
    i = DString_FindChar( libpath, '.', 0 );
    if( i != MAXSIZE ) DString_Erase( libpath, i, -1 );
    /* printf( "%s\n", libpath->mbs ); */
    if( reqns ){
      for(i=0; i<reqns->size; i++){
        node = MAP_Find( self->modRequire, reqns->items.pString[i] );
        /* printf( "requiring:  %p  %s\n", node, reqns->items.pString[i]->mbs ); */
        if( node ) DaoNameSpace_Import( ns, (DaoNameSpace*)node->value.pBase, NULL );
      }
    }
    /* MAP_Insert( self->modRequire, libpath, ns ); */
  }
  ns->libHandle = handle;

  dhv = (long*) DaoGetSymbolAddress( handle, "DaoH_Version" );
  if( dhv == NULL ){
    /* no warning or error, for loading a C/C++ dynamic linking library
      for solving symbols in Dao modules. */
    return ns;
  }else if( *dhv != DAO_H_VERSION ){
    char buf[200];
    sprintf( buf, "ERROR: DaoH_Version not matching, "
        "require \"%i\" but find \"%li\" in the library (%s).\n", 
        DAO_H_VERSION, *dhv, libpath->mbs );
    DaoStream_WriteMBS( self->stdStream, buf );
    return 0;
  }
  api = (DaoAPI*) DaoGetSymbolAddress( handle, "__dao" );
  if( api == NULL ){
    DaoStream_WriteMBS( self->stdStream, 
        "WARNING: Dao APIs are not available through wrapped interfaces.\n" );
  }else{
    DaoInitAPI( api );
  }
  
  funpter = (FuncType) DaoGetSymbolAddress( handle, "DaoOnLoad" );
  if( ! funpter ){
    DaoStream_WriteMBS( self->stdStream, "unable to find symbol DaoOnLoad in the library.\n");
    return 0;
  }
  (*funpter)( self, ns );
#if 0
  change to handle returned value of DaoOnLoad?
  if( bl ==0 ){
    MAP_Erase( self->nsModules, libpath );
    GC_DecRC( ns );
    return NULL;
  }
#endif
  return ns;
}
void DaoVmSpace_AddVirtualFile( DaoVmSpace *self, const char *file, const char *data )
{
  DNode *node;
  DString_ToMBS( self->srcFName );
  DString_SetMBS( self->srcFName, "/@/" );
  DString_AppendMBS( self->srcFName, file );
  node = DMap_Find( self->vfiles, self->srcFName );
  if( node ){
    DString_AppendMBS( node->value.pString, data );
  }else{
    DString_ToMBS( self->source );
    DString_SetMBS( self->source, data );
    MAP_Insert( self->vfiles, self->srcFName, self->source );
  }
}

int TestPath( DaoVmSpace *vms, DString *fname )
{
  FILE *file;
  DNode *node = MAP_Find( vms->vfiles, fname );
  /* printf( "testing: %s  %p\n", fname->mbs, node ); */
  if( node ) return 1;
  file = fopen( fname->mbs, "r" );
  if( file == NULL ) return 0;
  fclose( file );
  return 1;
}
void Dao_MakePath( DString *base, DString *path )
{
  while( DString_MatchMBS( path, " ^ %.%. / ", NULL, NULL ) ){
    if( DString_MatchMBS( base, " [^/] + ( / | ) $ ", NULL, NULL ) ){
      DString_ChangeMBS( path, " ^ %.%. / ", "", 1, NULL, NULL );
      DString_ChangeMBS( base, " [^/] + ( / |) $ ", "", 0, NULL, NULL );
    }else return;
  }
  if( base->size && path->size ){
    if( base->mbs[ base->size-1 ] != '/' && path->mbs[0] != '/' )
      DString_InsertChar( path, '/', 0 );
    DString_Insert( path, base, 0, 0 );
  }
  DString_ChangeMBS( path, "/ %. /", "/", 0, NULL, NULL );
}
void DaoVmSpace_MakePath( DaoVmSpace *self, DString *fname, int check )
{
  size_t i;
  char *p;
  DString *path;

  DString_ToMBS( fname );
  DString_ChangeMBS( fname, "/ %s* %. %s* /", "/", 0, NULL, NULL );
  DString_ChangeMBS( fname, "[^%./] + / %. %. /", "", 0, NULL, NULL );
  /* erase the last '/' */
  if( fname->size && fname->mbs[ fname->size-1 ] =='/' ){
    fname->size --;
    fname->mbs[ fname->size ] = 0;
  }

  /* C:\dir\source.dao; /home/...  */
  if( fname->size >1 && ( fname->mbs[0]=='/' || fname->mbs[1]==':' ) ) return;

  while( ( p = strchr( fname->mbs, '\\') ) !=NULL ) *p = '/';

  path = DString_Copy( self->pathWorking );

  /* ./source.dao; ../../source.dao */
  if( strstr( fname->mbs, "./" ) !=NULL || strstr( fname->mbs, "../" ) !=NULL ){

    if( self->pathLoading->size ){
      DString_Assign( path, self->pathLoading->items.pString[0] );
      if( path->size ==0 ) goto FreeString;
    }else if( self->pathWorking->size==0 ) goto FreeString;

    Dao_MakePath( path, fname );
    goto FreeString;
  }

  for( i=0; i<self->pathLoading->size; i++){
    int m = path->size;
    DString_Assign( path, self->pathLoading->items.pString[i] );
    if( m > 0 && path->mbs[ m-1 ] != '/' ) DString_AppendMBS( path, "/" );
    DString_Append( path, fname );
    /*
    printf( "%s %s\n", self->pathLoading->items.pString[i]->mbs, path->mbs );
    */
    if( TestPath( self, path ) ){
      DString_Assign( fname, path );
      goto FreeString;
    }
  }
  if( path->size > 0 && path->mbs[ path->size -1 ] != '/' )
    DString_AppendMBS( path, "/" );
  DString_Append( path, fname );
  /* printf( "%s %s\n", path->mbs, path->mbs ); */
  if( ! check || TestPath( self, path ) ){
    DString_Assign( fname, path );
    goto FreeString;
  }
  for( i=0; i<self->pathSearching->size; i++){
    DString_Assign( path, self->pathSearching->items.pString[i] );
    DString_AppendMBS( path, "/" );
    DString_Append( path, fname );
    /*
    printf( "%s %s\n", self->pathSearching->items.pString[i]->mbs, path->mbs );
    */
    if( TestPath( self, path ) ){
      DString_Assign( fname, path );
      goto FreeString;
    }
  }
FreeString:
  DString_Delete( path );
}
void DaoVmSpace_SetPath( DaoVmSpace *self, const char *path )
{
  char *p;
  DString_SetMBS( self->pathWorking, path );
  while( ( p = strchr( self->pathWorking->mbs, '\\') ) !=NULL ) *p = '/';
}
void DaoVmSpace_AddPath( DaoVmSpace *self, const char *path )
{
  DString *pstr;
  int exist = 0;
  char *p;
  size_t i;
  
  pstr = DString_New(1);
  DString_SetMBS( pstr, path );
  while( ( p = strchr( pstr->mbs, '\\') ) !=NULL ) *p = '/';
  DaoVmSpace_MakePath( self, pstr, 1 );

  if( pstr->mbs[pstr->size-1] == '/' ) DString_Erase( pstr, pstr->size-1, 1 );

  for(i=0; i<self->pathSearching->size; i++ ){
    if( DString_Compare( pstr, self->pathSearching->items.pString[i] ) == 0 ){
      exist = 1;
      break;
    }
  }
  if( ! exist ){
    DString *tmp = self->pathWorking;
    self->pathWorking = pstr;
    DArray_PushFront( self->pathSearching, pstr );
    DString_AppendMBS( pstr, "/addpath.dao" );
    if( TestPath( self, pstr ) ){
      DaoVmSpace_LoadDaoModuleExt( self, pstr, 1, 1 );
    }
    self->pathWorking = tmp;
  }
  /*
  for(i=0; i<self->pathSearching->size; i++ )
    printf( "%s\n", self->pathSearching->items.pString[i]->mbs );
  */
  DString_Delete( pstr );
}
void DaoVmSpace_DelPath( DaoVmSpace *self, const char *path )
{
  DString *pstr;
  char *p;
  int i, id = -1;

  pstr = DString_New(1);
  DString_SetMBS( pstr, path );
  while( ( p = strchr( pstr->mbs, '\\') ) !=NULL ) *p = '/';
  DaoVmSpace_MakePath( self, pstr, 1 );

  if( pstr->mbs[pstr->size-1] == '/' ) DString_Erase( pstr, pstr->size-1, 1 );

  for(i=0; i<self->pathSearching->size; i++ ){
    if( DString_Compare( pstr, self->pathSearching->items.pString[i] ) == 0 ){
      id = i;
      break;
    }
  }
  if( id >= 0 ){
    DString *pathDao = DString_Copy( pstr );
    DString *tmp = self->pathWorking;
    self->pathWorking = pstr;
    DString_AppendMBS( pathDao, "/delpath.dao" );
    if( TestPath( self, pathDao ) ){
      DaoVmSpace_LoadDaoModuleExt( self, pathDao, 1, 1 );
      /* id may become invalid after loadDaoModule(): */
      id = -1;
      for(i=0; i<self->pathSearching->size; i++ ){
        if( DString_Compare( pstr, self->pathSearching->items.pString[i] ) == 0 ){
          id = i;
          break;
        }
      }
    }
    DArray_Erase( self->pathSearching, id, 1 );
    DString_Delete( pathDao );
    self->pathWorking = tmp;
  }
  DString_Delete( pstr );
}

int IsValidName( const char *chs );
static void DaoRoutine_GetSignature( DaoAbsType *rt, DString *sig )
{
  DaoAbsType *it;
  int i;
  DString_Clear( sig );
  DString_ToMBS( sig );
  for(i=((rt->attrib & DAO_ROUT_PARSELF)!=0); i<rt->nested->size; i++){
    it = rt->nested->items.pAbtp[i];
    if( sig->size ) DString_AppendChar( sig, ',' );
    if( it->tid == DAO_PAR_NAMED || it->tid == DAO_PAR_DEFAULT ){
      DString_Append( sig, it->X.abtype->name );
    }else{
      DString_Append( sig, it->name );
    }
  }
}
static void DaoTypeBase_Free( DaoTypeBase *t )
{
  DaoTypeCore *core = t->priv;
  int i;
  for(i=0; i<core->valCount; i++){
    DString_Delete( core->values[i]->name );
    DValue_Clear( & core->values[i]->value );
    dao_free( core->values[i] );
  }
  for(i=0; i<core->methCount; i++){
    /* printf( "%3i: %s\n", core->methods[i]->refCount, core->methods[i]->routName->mbs ); */
    GC_DecRC( core->methods[i] );
  }
  dao_free( core->values );
  dao_free( core->methods );
}
extern DaoTypeBase libStandardTyper;
extern DaoTypeBase libMathTyper;
extern DaoTypeBase libMpiTyper;
extern DaoTypeBase libReflectTyper;
extern DaoTypeBase thdMasterTyper;
extern DaoTypeBase vmpTyper;
extern DaoTypeBase coroutTyper;

/* extern DaoTypeBase dao_DaoException_Typer; */

DaoObject* daoExceptionObjects[50];

DaoClass *daoClassException = NULL;
DaoClass *daoClassExceptionNone = NULL;
DaoClass *daoClassExceptionAny = NULL;

DaoClass *daoClassFutureValue = NULL;

extern DaoTypeBase DaoFdSet_Typer;

#ifndef DAO_WITH_THREAD
DaoMutex* DaoMutex_New( DaoVmSpace *vms ){ return NULL; }
void DaoMutex_Lock( DaoMutex *self ){}
void DaoMutex_Unlock( DaoMutex *self ){}
int DaoMutex_TryLock( DaoMutex *self ){ return 0; }
#endif

void DaoInitAPI( DaoAPI *api )
{
  if( api ==NULL ) return;
  memset( api, 0, sizeof( DaoAPI ) );

  api->DaoInit = DaoInit;
  api->DaoQuit = DaoQuit;
  api->DValue_NewInteger = DValue_NewInteger;
  api->DValue_NewFloat = DValue_NewFloat;
  api->DValue_NewDouble = DValue_NewDouble;
  api->DValue_NewMBString = DValue_NewMBString;
  api->DValue_NewWCString = DValue_NewWCString;
  api->DValue_NewVectorB = DValue_NewVectorB;
  api->DValue_NewVectorUB = DValue_NewVectorUB;
  api->DValue_NewVectorS = DValue_NewVectorS;
  api->DValue_NewVectorUS = DValue_NewVectorUS;
  api->DValue_NewVectorI = DValue_NewVectorI;
  api->DValue_NewVectorUI = DValue_NewVectorUI;
  api->DValue_NewVectorF = DValue_NewVectorF;
  api->DValue_NewVectorD = DValue_NewVectorD;
  api->DValue_NewMatrixB = DValue_NewMatrixB;
  api->DValue_NewMatrixUB = DValue_NewMatrixUB;
  api->DValue_NewMatrixS = DValue_NewMatrixS;
  api->DValue_NewMatrixUS = DValue_NewMatrixUS;
  api->DValue_NewMatrixI = DValue_NewMatrixI;
  api->DValue_NewMatrixUI = DValue_NewMatrixUI;
  api->DValue_NewMatrixF = DValue_NewMatrixF;
  api->DValue_NewMatrixD = DValue_NewMatrixD;
  api->DValue_NewBuffer = DValue_NewBuffer;
  api->DValue_NewStream = DValue_NewStream;
  api->DValue_NewCData = DValue_NewCData;
  api->DValue_Copy = DValue_Copy;
  api->DValue_Clear = DValue_Clear;
  api->DValue_ClearAll = DValue_ClearAll;

  api->DString_New = DString_New;
  api->DString_Delete = DString_Delete;

  api->DString_Size = DString_Size;
  api->DString_Clear = DString_Clear;
  api->DString_Resize = DString_Resize;

  api->DString_IsMBS = DString_IsMBS;
  api->DString_SetMBS = DString_SetMBS;
  api->DString_SetWCS = DString_SetWCS;
  api->DString_ToWCS = DString_ToWCS;
  api->DString_ToMBS = DString_ToMBS;
  api->DString_GetMBS = DString_GetMBS;
  api->DString_GetWCS = DString_GetWCS;

  api->DString_Erase = DString_Erase;
  api->DString_Insert = DString_Insert;
  api->DString_InsertMBS = DString_InsertMBS;
  api->DString_InsertChar = DString_InsertChar;
  api->DString_InsertWCS = DString_InsertWCS;
  api->DString_Append = DString_Append;
  api->DString_AppendChar = DString_AppendChar;
  api->DString_AppendWChar = DString_AppendWChar;
  api->DString_AppendMBS = DString_AppendMBS;
  api->DString_AppendWCS = DString_AppendWCS;
  api->DString_Substr = DString_Substr;

  api->DString_Find = DString_Find;
  api->DString_RFind = DString_RFind;
  api->DString_FindMBS = DString_FindMBS;
  api->DString_RFindMBS = DString_RFindMBS;
  api->DString_FindChar = DString_FindChar;
  api->DString_FindWChar = DString_FindWChar;
  api->DString_RFindChar = DString_RFindChar;

  api->DString_Copy = DString_Copy;
  api->DString_Assign = DString_Assign;
  api->DString_Compare = DString_Compare;

  api->DaoList_New = DaoList_New;
  api->DaoList_Size = DaoList_Size;
  api->DaoList_Front = DaoList_Front;
  api->DaoList_Back = DaoList_Back;
  api->DaoList_GetItem = DaoList_GetItem;

  api->DaoList_SetItem = DaoList_SetItem;
  api->DaoList_Insert = DaoList_Insert;
  api->DaoList_Erase = DaoList_Erase;
  api->DaoList_Clear = DaoList_Clear;
  api->DaoList_PushFront = DaoList_PushFront;
  api->DaoList_PushBack = DaoList_PushBack;
  api->DaoList_PopFront = DaoList_PopFront;
  api->DaoList_PopBack = DaoList_PopBack;

  api->DaoMap_New = DaoMap_New;
  api->DaoMap_Size = DaoMap_Size;
  api->DaoMap_Insert = DaoMap_Insert;
  api->DaoMap_Erase = DaoMap_Erase;
  api->DaoMap_Clear = DaoMap_Clear;
  api->DaoMap_InsertMBS = DaoMap_InsertMBS;
  api->DaoMap_InsertWCS = DaoMap_InsertWCS;
  api->DaoMap_EraseMBS = DaoMap_EraseMBS;
  api->DaoMap_EraseWCS = DaoMap_EraseWCS;
  api->DaoMap_GetValue = DaoMap_GetValue;
  api->DaoMap_GetValueMBS = DaoMap_GetValueMBS;
  api->DaoMap_GetValueWCS = DaoMap_GetValueWCS;
  api->DaoMap_First = DaoMap_First;
  api->DaoMap_Next = DaoMap_Next;
  api->DNode_Key = DNode_Key;
  api->DNode_Value = DNode_Value;
  
  api->DaoTuple_New = DaoTuple_New;
  api->DaoTuple_Size = DaoTuple_Size;
  api->DaoTuple_SetItem = DaoTuple_SetItem;
  api->DaoTuple_GetItem = DaoTuple_GetItem;

#ifdef DAO_WITH_NUMARRAY
  api->DaoArray_NumType = DaoArray_NumType;
  api->DaoArray_SetNumType = DaoArray_SetNumType;
  api->DaoArray_Size = DaoArray_Size;
  api->DaoArray_DimCount = DaoArray_DimCount;
  api->DaoArray_SizeOfDim = DaoArray_SizeOfDim;
  api->DaoArray_GetShape = DaoArray_GetShape;
  api->DaoArray_HasShape = DaoArray_HasShape;
  api->DaoArray_GetFlatIndex = DaoArray_GetFlatIndex;
  api->DaoArray_ResizeVector = DaoArray_ResizeVector;
  api->DaoArray_ResizeArray = DaoArray_ResizeArray;
  api->DaoArray_Reshape = DaoArray_Reshape;

  api->DaoArray_ToByte = DaoArray_ToByte;
  api->DaoArray_ToShort = DaoArray_ToShort;
  api->DaoArray_ToInt = DaoArray_ToInt;
  api->DaoArray_ToFloat = DaoArray_ToFloat;
  api->DaoArray_ToDouble = DaoArray_ToDouble;
  api->DaoArray_ToUByte = DaoArray_ToUByte;
  api->DaoArray_ToUShort = DaoArray_ToUShort;
  api->DaoArray_ToUInt = DaoArray_ToUInt;

  api->DaoArray_GetMatrixB = DaoArray_GetMatrixB;
  api->DaoArray_GetMatrixS = DaoArray_GetMatrixS;
  api->DaoArray_GetMatrixI = DaoArray_GetMatrixI;
  api->DaoArray_GetMatrixF = DaoArray_GetMatrixF;
  api->DaoArray_GetMatrixD = DaoArray_GetMatrixD;

  api->DaoArray_FromByte = DaoArray_FromByte;
  api->DaoArray_FromShort = DaoArray_FromShort;
  api->DaoArray_FromInt = DaoArray_FromInt;
  api->DaoArray_FromFloat = DaoArray_FromFloat;
  api->DaoArray_FromDouble = DaoArray_FromDouble;
  api->DaoArray_FromUByte = DaoArray_FromUByte;
  api->DaoArray_FromUShort = DaoArray_FromUShort;
  api->DaoArray_FromUInt = DaoArray_FromUInt;

  api->DaoArray_SetVectorB = DaoArray_SetVectorB;
  api->DaoArray_SetVectorS = DaoArray_SetVectorS;
  api->DaoArray_SetVectorI = DaoArray_SetVectorI;
  api->DaoArray_SetVectorF = DaoArray_SetVectorF;
  api->DaoArray_SetVectorD = DaoArray_SetVectorD;
  api->DaoArray_SetMatrixB = DaoArray_SetMatrixB;
  api->DaoArray_SetMatrixS = DaoArray_SetMatrixS;
  api->DaoArray_SetMatrixI = DaoArray_SetMatrixI;
  api->DaoArray_SetMatrixF = DaoArray_SetMatrixF;
  api->DaoArray_SetMatrixD = DaoArray_SetMatrixD;
  api->DaoArray_SetVectorUB = DaoArray_SetVectorUB;
  api->DaoArray_SetVectorUS = DaoArray_SetVectorUS;
  api->DaoArray_SetVectorUI = DaoArray_SetVectorUI;
  api->DaoArray_GetBuffer = DaoArray_GetBuffer;
  api->DaoArray_SetBuffer = DaoArray_SetBuffer;
#endif

  api->DaoObject_GetField = DaoObject_GetField;
  api->DaoObject_MapCData = DaoObject_MapCData;

  api->DaoStream_New = DaoStream_New;
  api->DaoStream_SetFile = DaoStream_SetFile;
  api->DaoStream_GetFile = DaoStream_GetFile;

  api->DaoFunction_Call = DaoFunction_Call;

  api->DaoCData_New = DaoCData_New;
  api->DaoCData_Wrap = DaoCData_Wrap;
  api->DaoCData_IsType = DaoCData_IsType;
  api->DaoCData_SetData = DaoCData_SetData;
  api->DaoCData_SetBuffer = DaoCData_SetBuffer;
  api->DaoCData_SetArray = DaoCData_SetArray;
  api->DaoCData_GetTyper = DaoCData_GetTyper;
  api->DaoCData_GetData = DaoCData_GetData;
  api->DaoCData_GetBuffer = DaoCData_GetBuffer;
  api->DaoCData_GetData2 = DaoCData_GetData2;
  api->DaoCData_GetObject = DaoCData_GetObject;

  api->DaoMutex_New = DaoMutex_New;
  api->DaoMutex_Lock = DaoMutex_Lock;
  api->DaoMutex_Unlock = DaoMutex_Unlock;
  api->DaoMutex_TryLock = DaoMutex_TryLock;

  api->DaoContext_PutInteger = DaoContext_PutInteger;
  api->DaoContext_PutFloat = DaoContext_PutFloat;
  api->DaoContext_PutDouble = DaoContext_PutDouble;
  api->DaoContext_PutComplex = DaoContext_PutComplex;
  api->DaoContext_PutMBString = DaoContext_PutMBString;
  api->DaoContext_PutWCString = DaoContext_PutWCString;
  api->DaoContext_PutString = DaoContext_PutString;
  api->DaoContext_PutBytes = DaoContext_PutBytes;
  api->DaoContext_PutArrayInteger = DaoContext_PutArrayInteger;
  api->DaoContext_PutArrayShort = DaoContext_PutArrayShort;
  api->DaoContext_PutArrayFloat = DaoContext_PutArrayFloat;
  api->DaoContext_PutArrayDouble = DaoContext_PutArrayDouble;
  api->DaoContext_PutArrayComplex = DaoContext_PutArrayComplex;
  api->DaoContext_PutList = DaoContext_PutList;
  api->DaoContext_PutMap = DaoContext_PutMap;
  api->DaoContext_PutArray = DaoContext_PutArray;
  api->DaoContext_PutFile = DaoContext_PutFile;
  api->DaoContext_PutCData = DaoContext_PutCData;
  api->DaoContext_PutCPointer = DaoContext_PutCPointer;
  api->DaoContext_PutResult = DaoContext_PutResult;
  api->DaoContext_WrapCData = DaoContext_WrapCData;
  api->DaoContext_CopyCData = DaoContext_CopyCData;
    api->DaoContext_PutValue = DaoContext_PutValue;
    api->DaoContext_RaiseException = DaoContext_RaiseException;

  api->DaoVmProcess_New = DaoVmProcess_New;
  api->DaoVmProcess_Compile = DaoVmProcess_Compile;
  api->DaoVmProcess_Eval = DaoVmProcess_Eval;
  api->DaoVmProcess_Call = DaoVmProcess_Call;
  api->DaoVmProcess_GetReturned = DaoVmProcess_GetReturned;

  api->DaoNameSpace_New = DaoNameSpace_New;
  api->DaoNameSpace_GetNameSpace = DaoNameSpace_GetNameSpace;
  api->DaoNameSpace_AddConstNumbers = DaoNameSpace_AddConstNumbers;
  api->DaoNameSpace_AddConstValue = DaoNameSpace_AddConstValue;
  api->DaoNameSpace_AddConstData = DaoNameSpace_AddConstData;
  api->DaoNameSpace_AddData = DaoNameSpace_AddData;
  api->DaoNameSpace_AddValue = DaoNameSpace_AddValue;
  api->DaoNameSpace_FindData = DaoNameSpace_FindData;

  api->DaoNameSpace_TypeDefine = DaoNameSpace_TypeDefine;
  api->DaoNameSpace_TypeDefines = DaoNameSpace_TypeDefines;
  api->DaoNameSpace_AddType = DaoNameSpace_AddType;
  api->DaoNameSpace_AddTypes = DaoNameSpace_AddTypes;
  api->DaoNameSpace_AddFunction = DaoNameSpace_AddFunction;
  api->DaoNameSpace_AddFunctions = DaoNameSpace_AddFunctions;
  api->DaoNameSpace_SetupType = DaoNameSpace_SetupType;
  api->DaoNameSpace_SetupTypes = DaoNameSpace_SetupTypes;
  api->DaoNameSpace_Load = DaoNameSpace_Load;

  api->DaoVmSpace_New = DaoVmSpace_New;
  api->DaoVmSpace_ParseOptions = DaoVmSpace_ParseOptions;
  api->DaoVmSpace_SetOptions = DaoVmSpace_SetOptions;
  api->DaoVmSpace_GetOptions = DaoVmSpace_GetOptions;

  api->DaoVmSpace_RunMain = DaoVmSpace_RunMain;
  api->DaoVmSpace_Load = DaoVmSpace_Load;
  api->DaoVmSpace_MainNameSpace = DaoVmSpace_MainNameSpace;
  api->DaoVmSpace_MainVmProcess = DaoVmSpace_MainVmProcess;

  api->DaoVmSpace_SetUserHandler = DaoVmSpace_SetUserHandler;
  api->DaoVmSpace_ReadLine = DaoVmSpace_ReadLine;
  api->DaoVmSpace_AddHistory = DaoVmSpace_AddHistory;

  api->DaoVmSpace_SetPath = DaoVmSpace_SetPath;
  api->DaoVmSpace_AddPath = DaoVmSpace_AddPath;
  api->DaoVmSpace_DelPath = DaoVmSpace_DelPath;

  api->DaoVmSpace_GetState = DaoVmSpace_GetState;
  api->DaoVmSpace_Stop = DaoVmSpace_Stop;

  api->DaoGC_IncRC = DaoGC_IncRC;
  api->DaoGC_DecRC = DaoGC_DecRC;
}
extern void DaoInitLexTable();

static void DaoConfigure_FromFile( const char *name )
{
  double number;
  int i, ch, isnum, isint, integer=0, yes;
  FILE *fin = fopen( name, "r" );
  DaoToken *tk1, *tk2;
  DString *mbs;
  DArray *tokens;
  if( fin == NULL ) return;
  mbs = DString_New(1);
  tokens = DArray_New( D_TOKEN );
  while( ( ch=getc(fin) ) != EOF ) DString_AppendChar( mbs, ch );
  fclose( fin );
  DString_ToLower( mbs );
  DaoToken_Tokenize( tokens, mbs->mbs, 1, 0, 0 );
  i = 0;
  while( i < tokens->size ){
    tk1 = tokens->items.pToken[i];
    /* printf( "%s\n", tk1->string->mbs ); */
    if( tk1->type == DTOK_IDENTIFIER ){
      if( i+2 >= tokens->size ) goto InvalidConfig;
      if( tokens->items.pToken[i+1]->type != DTOK_ASSN ) goto InvalidConfig;
      tk2 = tokens->items.pToken[i+2];
      isnum = isint = 0;
      yes = -1;
      if( tk2->type >= DTOK_DIGITS_HEX && tk2->type <= DTOK_NUMBER_SCI ){
        isnum = 1;
        if( tk2->type <= DTOK_NUMBER_HEX ){
          isint = 1;
          number = integer = strtol( tk2->string->mbs, NULL, 0 );
        }else{
          number = strtod( tk2->string->mbs, NULL );
        }
      }else if( tk2->type == DTOK_IDENTIFIER ){
        if( TOKCMP( tk2, "yes" )==0 )  yes = 1;
        if( TOKCMP( tk2, "no" )==0 ) yes = 0;
      }
      if( TOKCMP( tk1, "cpu" )==0 ){
        /* printf( "%s  %i\n", tk2->string->mbs, tk2->type ); */
        if( isint == 0 ) goto InvalidConfigValue;
        daoConfig.cpu = integer;
      }else if( TOKCMP( tk1, "jit" )==0 ){
        if( yes <0 ) goto InvalidConfigValue;
        daoConfig.jit = yes;
      }else if( TOKCMP( tk1, "safe" )==0 ){
        if( yes <0 ) goto InvalidConfigValue;
        daoConfig.safe = yes;
      }else if( TOKCMP( tk1, "typedcode" )==0 ){
        if( yes <0 ) goto InvalidConfigValue;
        daoConfig.typedcode = yes;
      }else if( TOKCMP( tk1, "incompile" )==0 ){
        if( yes <0 ) goto InvalidConfigValue;
        daoConfig.incompile = yes;
      }else{
        goto InvalidConfigName;
      }
      i += 3;
      continue;
    }else if( tk1->type == DTOK_COMMA || tk1->type == DTOK_SEMCO ){
      i ++;
      continue;
    }
InvalidConfig :
    printf( "error: invalid configuration file format at line: %i!\n", tk1->line );
    break;
InvalidConfigName :
    printf( "error: invalid configuration option name: %s!\n", tk1->string->mbs );
    break;
InvalidConfigValue :
    printf( "error: invalid configuration option value: %s!\n", tk2->string->mbs );
    break;
  }
  DArray_Delete( tokens );
  DString_Delete( mbs );
}
static void DaoConfigure()
{
  char *daodir = getenv( "DAO_DIR" );
  DString *mbs = DString_New(1);

  DaoInitLexTable();
  daoConfig.cpu = daoConfig.jit = daoConfig.typedcode = daoConfig.incompile = 1;
  daoConfig.safe = 0;
  daoConfig.iscgi = getenv( "GATEWAY_INTERFACE" ) ? 1 : 0;

  DString_SetMBS( mbs, DAO_PATH );
  DString_AppendMBS( mbs, "/dao.conf" );
  DaoConfigure_FromFile( mbs->mbs );
  if( daodir ){
    DString_SetMBS( mbs, daodir );
    if( daodir[ mbs->size-1 ] == '/' ){
      DString_AppendMBS( mbs, "dao.conf" );
    }else{
      DString_AppendMBS( mbs, "/dao.conf" );
    }
    DaoConfigure_FromFile( mbs->mbs );
  }
  DaoConfigure_FromFile( "./dao.conf" );
  DString_Delete( mbs );
}

extern void DaoJitMapper_Init();
extern void DaoAbsType_Init();

DaoAbsType *dao_type_udf = NULL;
DaoAbsType *dao_array_bit = NULL;
DaoAbsType *dao_array_any = NULL;
DaoAbsType *dao_list_any = NULL;
DaoAbsType *dao_map_any = NULL;
DaoAbsType *dao_routine = NULL;
DaoAbsType *dao_type_for_iterator = NULL;

#ifdef DAO_WITH_THREAD
extern DMutex  mutex_string_sharing;
#endif

DaoVmSpace* DaoInit()
{
  int i;
  DaoVmSpace *vms;
  DaoNameSpace *ns;
  DString *mbs;
  DArray *nested;
#if 0
  DaoTypeBase *typers[] = { 
    & stringTyper, & comTyper, & longTyper, & listTyper, & mapTyper,
    & streamTyper, & vmpTyper, & coroutTyper, & libStandardTyper, 
    & libMathTyper, & libMpiTyper, & libReflectTyper
#ifdef DAO_WITH_NUMARRAY
    & numarTyper,
#endif
#ifdef DAO_WITH_THREAD
    & mutexTyper, & condvTyper, & semaTyper, & threadTyper, & thdMasterTyper, 
#endif
    NULL };
#endif

  if( mainVmSpace ) return mainVmSpace;

#ifdef DAO_WITH_THREAD
  DMutex_Init( & mutex_string_sharing );
#endif
  
  mbs = DString_New(1);
  setlocale( LC_CTYPE, "" );
  DaoConfigure();
  DaoAbsType_Init();
  /*
  printf( "number of VM instructions: %i\n", DVM_NULL );
  */

#ifdef DEBUG 
  for(i=0; i<100; i++) ObjectProfile[i] = 0;
#endif

#ifdef DAO_WITH_THREAD
  DaoInitThread();
#endif

#ifdef DAO_WITH_JIT
  DaoJitMapper_Init();
#endif
  
  DaoStartGC();

  nested = DArray_New(0);
  dao_type_udf = DaoAbsType_New( "?", DAO_UDF, NULL, NULL );
  DArray_Append( nested, dao_type_udf );
  dao_routine = DaoAbsType_New( "routine<=>?>", DAO_ROUTINE, NULL, nested );
  DArray_Delete( nested );

  mainVmSpace = vms = DaoVmSpace_New();
  vms->safeTag = 0;
  ns = vms->nsInternal;

  dao_type_for_iterator = DaoParser_ParseTypeName( "tuple<valid:int,iterator:any>", ns, 0,0 );
  DString_SetMBS( dao_type_for_iterator->name, "for_iterator" );
  DaoNameSpace_AddAbsType( ns, dao_type_for_iterator->name, dao_type_for_iterator );

  dao_array_any = DaoParser_ParseTypeName( "array<any>", ns, 0,0 );
  dao_list_any = DaoParser_ParseTypeName( "list<any>", ns, 0,0 );
  dao_map_any = DaoParser_ParseTypeName( "map<any,any>", ns, 0,0 );
  dao_array_bit = DaoParser_ParseTypeName( "long", ns, 0,0 );
  dao_array_bit = DaoAbsType_Copy( dao_array_bit );
  DString_SetMBS( dao_array_bit->name, "bitarray" );
  DaoNameSpace_AddAbsType( ns, dao_array_bit->name, dao_array_bit );
  
#if 1
  /*
  DaoVmSpace_AddType( vms, & dao_DaoException_Typer );
  */

  DString_SetMBS( vms->source, daoScripts );
  DString_SetMBS( vms->srcFName, "internal scripts" );
  DaoVmProcess_Eval( vms->mainProcess, vms->nsInternal, vms->source, 0 );

  DString_SetMBS( mbs, "Exception" );
  daoClassException = DaoNameSpace_GetData( vms->nsInternal, mbs ).v.klass;
  for(i=0; i<ENDOF_BASIC_EXCEPT; i++ ){
    DString_SetMBS( mbs, daoExceptionName[i] );
    daoExceptionObjects[i] = DaoNameSpace_GetData( vms->nsInternal, mbs ).v.object;
    GC_IncRC( daoExceptionObjects[i] );
  }
  daoClassExceptionNone = daoExceptionObjects[ DAO_EXCEPT_NONE ]->myClass;
  daoClassExceptionAny = daoExceptionObjects[ DAO_EXCEPT_ANY ]->myClass;
  GC_IncRC( daoClassException );
  GC_IncRC( daoClassExceptionNone );
  GC_IncRC( daoClassExceptionAny );
  DString_SetMBS( mbs, "FutureValue" );
  daoClassFutureValue = DaoNameSpace_GetData( vms->nsInternal, mbs ).v.klass;
  GC_IncRC( daoClassFutureValue );

#else
  for(i=0; i<ENDOF_BASIC_EXCEPT; i++ ) daoExceptionObjects[i] = NULL;
#endif
  vms->nsWorking = vms->nsInternal;


#if 1

#ifdef DAO_WITH_NUMARRAY
  DaoNameSpace_PrepareType( vms->nsWorking, & numarTyper );
#endif


  DaoNameSpace_PrepareType( vms->nsWorking, & stringTyper );
  DaoNameSpace_PrepareType( vms->nsWorking, & longTyper );
  DaoNameSpace_PrepareType( vms->nsWorking, & comTyper );
  DaoNameSpace_PrepareType( vms->nsWorking, & listTyper );
  DaoNameSpace_PrepareType( vms->nsWorking, & mapTyper );

  DaoNameSpace_PrepareType( vms->nsWorking, & streamTyper );
  DaoNameSpace_AddType( vms->nsInternal, & cdataTyper, 1 );

#ifdef DAO_WITH_THREAD
  DaoNameSpace_MakeAbsType( ns, "thread", DAO_THREAD, NULL, NULL, 0 );
  DaoNameSpace_MakeAbsType( ns, "mtlib", DAO_THDMASTER, NULL, NULL, 0 );
  DaoNameSpace_MakeAbsType( ns, "mutex", DAO_MUTEX, NULL, NULL, 0 );
  DaoNameSpace_MakeAbsType( ns, "condition", DAO_CONDVAR, NULL, NULL, 0 );
  DaoNameSpace_MakeAbsType( ns, "semaphore", DAO_SEMA, NULL, NULL, 0 );
  DaoNameSpace_PrepareType( ns, & threadTyper );
  DaoNameSpace_PrepareType( ns, & thdMasterTyper );
  DaoNameSpace_PrepareType( ns, & mutexTyper );
  DaoNameSpace_PrepareType( ns, & condvTyper );
  DaoNameSpace_PrepareType( ns, & semaTyper );
#endif
  DaoNameSpace_PrepareType( vms->nsWorking, & vmpTyper );
  DaoNameSpace_PrepareType( vms->nsWorking, & coroutTyper );

  DaoNameSpace_PrepareType( vms->nsWorking, & libStandardTyper );
  DaoNameSpace_PrepareType( vms->nsWorking, & libMathTyper );
  DaoNameSpace_PrepareType( vms->nsWorking, & libMpiTyper );
  DaoNameSpace_PrepareType( vms->nsWorking, & libReflectTyper );
#endif

#if( defined DAO_WITH_THREAD && ( defined DAO_WITH_MPI || defined DAO_WITH_AFC ) )
  DaoSched_Init( vms );
#endif

#ifdef DAO_WITH_NETWORK
  DaoNameSpace_AddType( vms->nsWorking, & DaoFdSet_Typer, 1 );
  DaoNameSpace_PrepareType( vms->nsWorking, & libNetTyper );
  DaoNetwork_Init( vms, vms->nsWorking );
#endif
  DaoNameSpace_Import( vms->mainNamespace, vms->nsInternal, NULL );

  DaoVmSpace_AddPath( vms, DAO_PATH );
  /*
  printf( "initialized...\n" );
  */
  DString_Delete( mbs );
  vms->nsWorking = NULL;
  vms->safeTag = 1;
  return vms;
}
extern DaoAbsType* DaoParser_ParseTypeName( const char *type, DaoNameSpace *ns, DaoClass *cls, DaoRoutine *rout );
void DaoQuit()
{
  int i;
  /* TypeTest(); */
#if( defined DAO_WITH_THREAD && ( defined DAO_WITH_MPI || defined DAO_WITH_AFC ) )
  DaoSched_Join( mainVmSpace );
#endif

#ifdef DAO_WITH_THREAD
  DaoStopThread( mainVmSpace->thdMaster );
#endif

  if( daoConfig.iscgi ) return;

#ifdef DAO_WITH_NUMARRAY
  DaoTypeBase_Free( & numarTyper );
#endif

  DaoTypeBase_Free( & stringTyper );
  DaoTypeBase_Free( & longTyper );
  DaoTypeBase_Free( & comTyper );
  DaoTypeBase_Free( & listTyper );
  DaoTypeBase_Free( & mapTyper );

  DaoTypeBase_Free( & streamTyper );

#ifdef DAO_WITH_THREAD
  DaoTypeBase_Free( & mutexTyper );
  DaoTypeBase_Free( & condvTyper );
  DaoTypeBase_Free( & semaTyper );
  DaoTypeBase_Free( & threadTyper );
  DaoTypeBase_Free( & thdMasterTyper );
#endif
  DaoTypeBase_Free( & vmpTyper );
  DaoTypeBase_Free( & coroutTyper );

  DaoTypeBase_Free( & libStandardTyper );
  DaoTypeBase_Free( & libMathTyper );
  DaoTypeBase_Free( & libMpiTyper );
  DaoTypeBase_Free( & libReflectTyper );

#ifdef DAO_WITH_NETWORK
  /* DaoTypeBase_Free( & DaoFdSet_Typer ); */
  DaoTypeBase_Free( & libNetTyper );
#endif

  for(i=0; i<ENDOF_BASIC_EXCEPT; i++ ) GC_DecRC( daoExceptionObjects[i] );
  GC_DecRC( daoClassException );
  GC_DecRC( daoClassExceptionNone );
  GC_DecRC( daoClassExceptionAny );
  GC_DecRC( daoClassFutureValue );

  /* printf( "%i\n", mainVmSpace->nsWorking->refCount ); */
  /* DaoVmSpace_Delete( mainVmSpace ); */
  DaoFinishGC();
}
DaoNameSpace* DaoVmSpace_LoadModule( DaoVmSpace *self, DString *fname, DArray *reqns )
{
  DNode *node = MAP_Find( self->nsModules, fname );
  DaoNameSpace *ns = NULL;
  int modtype;
#ifdef DAO_WITH_THREAD
#endif
  if( node ) return (DaoNameSpace*) node->value.pBase;
  modtype = DaoVmSpace_CompleteModuleName( self, fname );
#if 0
  printf( "modtype = %i\n", modtype );
#endif
  if( modtype == DAO_MODULE_DAO_O )
    ns = DaoVmSpace_LoadDaoByteCode( self, fname, 1 );
  else if( modtype == DAO_MODULE_DAO_S )
    ns = DaoVmSpace_LoadDaoAssembly( self, fname, 1 );
  else if( modtype == DAO_MODULE_DAO )
    ns = DaoVmSpace_LoadDaoModule( self, fname );
  else if( modtype == DAO_MODULE_DLL )
    ns = DaoVmSpace_LoadDllModule( self, fname, reqns );
#ifdef DAO_WITH_THREAD
#endif
  return ns;
}

#ifdef UNIX
#include<dlfcn.h>
#elif WIN32
#include<windows.h>
#endif

void DaoGetErrorDLL()
{
#ifdef UNIX
  printf( "%s\n", dlerror() );
#elif WIN32
  DWORD error = GetLastError();
  LPSTR message;
  FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
      NULL, error, LANG_NEUTRAL, (LPTSTR)&message, 0, NULL );
  if( message ){
    printf( "%s\n", message );
    LocalFree( message );
  }
#endif
}

void* DaoOpenDLL( const char *name )
{
#ifdef UNIX
  void *handle = dlopen( name, RTLD_NOW | RTLD_GLOBAL );
#elif WIN32
  void *handle = LoadLibrary( name );
#endif
  if( !handle ){
    DaoGetErrorDLL();
    return 0;
  }
  return handle;
}
void* DaoGetSymbolAddress( void *handle, const char *name )
{
#ifdef UNIX
  void *sym = dlsym( handle, name );
#elif WIN32
  void *sym = (void*)GetProcAddress( (HMODULE)handle, name );
#endif
  return sym;
}

DaoObject* DaoException_GetObject( int type )
{
  if( type < 0 || type >= ENDOF_BASIC_EXCEPT ) type = DAO_ERROR;
  return daoExceptionObjects[type];
}