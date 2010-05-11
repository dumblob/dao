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

#include"stdio.h"
#include"string.h"

#include"daoObject.h"
#include"daoClass.h"
#include"daoRoutine.h"
#include"daoContext.h"
#include"daoProcess.h"
#include"daoVmspace.h"
#include"daoGC.h"
#include"daoStream.h"
#include"daoNumeric.h"

int DaoObject_InvokeMethod( DaoObject *self, DaoObject *thisObject,
    DaoVmProcess *vmp, DString *name, DaoContext *ctx, DValue par[], int N, int ret )
{
  DValue *ps[ DAO_MAX_PARAM+1 ];
  DValue value = daoNilValue;
  DValue selfpar = daoNilObject;
  int i, errcode = DaoObject_GetData( self, name, & value, thisObject, NULL );
  if( errcode ) return errcode;
  selfpar.v.object = self;
  if( value.t == DAO_ROUTINE ){
    DaoRoutine *rout = value.v.routine;
    DaoContext *ctxNew = DaoVmProcess_MakeContext( vmp, rout );
    for(i=0; i<=N; i++) ps[i] = par + i;
    GC_ShiftRC( self, ctxNew->object );
    ctxNew->object = self;
    DaoContext_Init( ctxNew, rout );
    DaoContext_InitWithParams( ctxNew, vmp, ps, N );
    if( DRoutine_PassParams( (DRoutine*) ctxNew->routine, &selfpar, 
          ctxNew->regValues, ps, NULL, N, DVM_CALL ) ){
      if( STRCMP( name, "_PRINT" ) ==0 ){
        DaoVmProcess_PushContext( ctx->process, ctxNew );
        DaoVmProcess_Execute( ctx->process );
      }else{
        DaoVmProcess_PushContext( ctx->process, ctxNew );
        if( ret > -10 ) ctx->process->topFrame->returning = (ushort_t) ret;
      }
      return 0;
    }
    DaoVmProcess_CacheContext( vmp, ctxNew );
    return DAO_ERROR_PARAM;
  }else if( value.t == DAO_FUNCTION ){
    DaoFunction *func = value.v.func;
    DValue p[ DAO_MAX_PARAM+1 ];
    p[0] = daoNilValue;
    memcpy( p+1, par, N*sizeof(DValue) );
    p[0].v.object = (DaoObject*) DaoObject_MapThisObject( self, NULL, func->hostCData->typer );
    p[0].t = p[0].v.object ? p[0].v.object->type : 0;
    for(i=0; i<=N; i++) ps[i] = p + i;
    func = (DaoFunction*)DRoutine_GetOverLoad( (DRoutine*) func, vmp, &selfpar, ps, N+1, DVM_MCALL );
    DaoFunction_SimpleCall( func, ctx, ps, N+1 );
  }
  return 0;
}
static void DaoObject_Print( DValue *self0, DaoContext *ctx, DaoStream *stream, DMap *cycData )
{
  DaoObject *self = self0->v.object;
  DValue pars = daoNilStream;
  int ec;
  pars.v.stream = stream;
  DString_SetMBS( ctx->process->mbstring, "_PRINT" );
  ec = DaoObject_InvokeMethod( self, ctx->object, ctx->process,
      ctx->process->mbstring, ctx, & pars, 1, -1 );
  if( ec && ec != DAO_ERROR_FIELD_NOTEXIST ){
    DaoContext_RaiseException( ctx, ec, DString_GetMBS( ctx->process->mbstring ) );
  }else if( ec == DAO_ERROR_FIELD_NOTEXIST ){
    char buf[50];
    sprintf( buf, "[%p]", self );
    DaoStream_WriteString( stream, self->myClass->className );
    DaoStream_WriteMBS( stream, buf );
  }
}
static void DaoObject_Core_GetField( DValue *self0, DaoContext *ctx, DString *name )
{
  DaoObject *self = self0->v.object;
  DValue *d2 = NULL;
  DValue value = daoNilValue;
  int rc = DaoObject_GetData( self, name, & value, ctx->object, & d2 );
  if( rc ){
    DString_SetMBS( ctx->process->mbstring, "." );
    DString_Append( ctx->process->mbstring, name );
    rc = DaoObject_InvokeMethod( self, ctx->object, ctx->process,
        ctx->process->mbstring, ctx, NULL, 0, -100 );
  }else{
    DaoContext_PutReference( ctx, d2 );
  }
  if( rc ) DaoContext_RaiseException( ctx, rc, DString_GetMBS( name ) );
}
static void DaoObject_Core_SetField( DValue *self0, DaoContext *ctx, DString *name, DValue value )
{
  DaoObject *self = self0->v.object;
  int ec;
  ec = DaoObject_SetData( self, name, value, ctx->object );
  if( ec ){
    DString_SetMBS( ctx->process->mbstring, "." );
    DString_Append( ctx->process->mbstring, name );
    DString_AppendMBS( ctx->process->mbstring, "=" );
    ec = DaoObject_InvokeMethod( self, ctx->object, ctx->process,
        ctx->process->mbstring, ctx, & value, 1, -1 );
  }
  if( ec ) DaoContext_RaiseException( ctx, ec, DString_GetMBS( name ) );
}
static void DaoObject_GetItem( DValue *self0, DaoContext *ctx, DValue pid )
{
  DaoObject *self = self0->v.object;
  int rc;
  DString_SetMBS( ctx->process->mbstring, "[]" );
  if( pid.t == DAO_TUPLE && pid.v.tuple->unitype != dao_type_for_iterator ){
    rc = DaoObject_InvokeMethod( self, ctx->object, ctx->process,
        ctx->process->mbstring, ctx, pid.v.tuple->items->data, 
        pid.v.tuple->items->size, -100 );
  }else{
    rc = DaoObject_InvokeMethod( self, ctx->object, ctx->process,
        ctx->process->mbstring, ctx, & pid, 1, -100 );
  }
  if( rc ) DaoContext_RaiseException( ctx, rc, DString_GetMBS( ctx->process->mbstring ) );
}
static void DaoObject_SetItem( DValue *self0, DaoContext *ctx, DValue pid, DValue value )
{
  DaoObject *self = self0->v.object;
  DValue par[ DAO_MAX_PARAM ];
  int rc, N = 1;
  par[0] = value;
  par[1] = pid;
  DString_SetMBS( ctx->process->mbstring, "[]=" );
  if( pid.t == DAO_TUPLE ){
    N = pid.v.tuple->items->size;
    memcpy( par + 1, pid.v.tuple->items->data,  N*sizeof(DValue) );
  }
  rc = DaoObject_InvokeMethod( self, ctx->object, ctx->process,
      ctx->process->mbstring, ctx, par, N+1, -1 );
  if( rc ) DaoContext_RaiseException( ctx, rc, DString_GetMBS( ctx->process->mbstring ) );
}
extern void DaoCopyValues( DValue *copy, DValue *data, int N, DaoContext *ctx, DMap *cycData );
static void DaoObject_CopyData(  DaoObject *self, DaoObject *from, DaoContext *ctx, DMap *cycData )
{
  DaoObject **selfSups = NULL;
  DaoObject **fromSups = NULL;
  DValue *selfValues = self->objValues;
  DValue *fromValues = from->objValues;
  int i, selfSize = self->myClass->objDataDefault->size;
  DaoCopyValues( selfValues + 1, fromValues + 1, selfSize-1, ctx, cycData );
  /*  XXX super might be CData: */
  if( from->superObject ==NULL ) return;
  selfSups = self->superObject->items.pObject;
  fromSups = from->superObject->items.pObject;
  for( i=0; i<from->superObject->size; i++ )
    DaoObject_CopyData( (DaoObject*) selfSups[i], (DaoObject*) fromSups[i], ctx, cycData );
}
static DValue DaoObject_Copy(  DValue *value, DaoContext *ctx, DMap *cycData )
{
  DaoObject *pnew, *self = value->v.object;
  DValue res = daoNilObject;
  DNode *node = DMap_Find( cycData, self );
  if( node ){
    res.v.p = node->value.pBase;
    return res;
  }

  pnew = DaoObject_New( self->myClass, NULL, 0 );
  res.v.object = pnew;
  DMap_Insert( cycData, self, pnew );
  DaoObject_CopyData( pnew, self, ctx, cycData );

  return res;
}

static DaoTypeCore objCore = 
{
  0,
#ifdef DEV_HASH_LOOKUP
  NULL, NULL,
#endif
  NULL, NULL, NULL, 0, 0,
  DaoObject_Core_GetField,
  DaoObject_Core_SetField,
  DaoObject_GetItem,
  DaoObject_SetItem,
  DaoObject_Print,
  DaoObject_Copy
};

DaoTypeBase objTyper=
{
  & objCore,
  "OBJECT",
  NULL, NULL, {0}, NULL,
  (FuncPtrDel) DaoObject_Delete
};

DaoObject* DaoObject_New( DaoClass *klass, DaoObject *that, int offset )
{
  DaoObject *self = (DaoObject*) dao_malloc( sizeof( DaoObject ) );
  int i;

  DaoBase_Init( self, DAO_OBJECT );
  self->myClass = klass;
  self->objData = NULL;
  self->superObject = NULL;
  if( that ){
    self->that = that;
    self->objValues = that->objData->data + offset;
  }else{
    self->that = self;
    self->objData = DVaTuple_New( klass->objDataName->size, daoNilValue );
    self->objValues = self->objData->data;
  }
  offset += 1;
  if( klass->superClass->size ){
    self->superObject = DPtrTuple_New( klass->superClass->size, NULL );
    for(i=0; i<klass->superClass->size; i++){
      DaoObject *sup = NULL;
      if( klass->superClass->items.pClass[i]->type == DAO_CLASS ){
        sup = DaoObject_New( klass->superClass->items.pClass[i], self->that, offset );
        sup->refCount ++;
        offset += sup->myClass->objDataName->size;
      }
      self->superObject->items.pObject[i] = sup;
    }
  }
  self->objValues[0].t = DAO_OBJECT;
  self->objValues[0].v.object = self;
  GC_IncRC( self );
  if( self->objData == NULL ) return self;
  for(i=1; i<klass->objDataDefault->size; i++){
    DaoAbsType *type = klass->objDataType->items.pAbtp[i];
    DValue *value = self->objValues + i;
    /* for data type such as list/map/array, 
     * its .unitype may need to be set properaly */
    if( klass->objDataDefault->data[i].t ){
      DValue_Move( klass->objDataDefault->data[i], value, type );
    }
    if( value->t ==0 && type ){
      if( type->tid <= DAO_DOUBLE ){
        value->t = type->tid;
      }else if( type->tid == DAO_COMPLEX ){
        value->t = type->tid;
        value->v.c = dao_calloc( 1, sizeof(complex16) );
      }else if( type->tid == DAO_STRING ){
        value->t = type->tid;
        value->v.s = DString_New(1);
      }else if( type->tid == DAO_LONG ){
        value->t = type->tid;
        value->v.l = DLong_New();
        if( type == dao_array_bit ) value->v.l->bits = 1;
      }
    }
  }
  return self;
}
void DaoObject_Delete( DaoObject *self )
{
  if( self->objData ) DVaTuple_Delete( self->objData );
  if( self->superObject ){
    int i;
    for(i=0; i<self->superObject->size; i++)
      GC_DecRC( self->superObject->items.pBase[i] );
    DPtrTuple_Delete( self->superObject );
  }
  dao_free( self );
}

DaoClass* DaoObject_MyClass( DaoObject *self )
{
  return self->myClass;
}
int DaoObject_ChildOf( DaoObject *self, DaoObject *obj )
{
  int i;
  if( obj == self ) return 1;
  if( self->type == DAO_CDATA ){
    if( obj->type == DAO_CDATA ){
      DaoCData *cdata1 = (DaoCData*) self;
      DaoCData *cdata2 = (DaoCData*) obj;
      if( DaoCData_ChildOf( cdata1->typer, cdata2->typer ) ) return 1;
    }
    return 0;
  }
  if( self->superObject ==NULL ) return 0;
  for( i=0; i<self->superObject->size; i++ ){
    if( obj == self->superObject->items.pObject[i] ) return 1;
    if( DaoObject_ChildOf( self->superObject->items.pObject[i], obj ) ) return 1;
  }
  return 0;
}
extern int DaoCData_ChildOf( DaoTypeBase *self, DaoTypeBase *super );

DaoBase* DaoObject_MapThisObject( DaoObject *self, DaoClass *host, DaoTypeBase *typer )
{
  DaoObject *obj;
  int i;
  if( self->myClass == host ) return (DaoBase*) self;
  if( self->superObject ==NULL ) return NULL;
  for( i=0; i<self->superObject->size; i++ ){
    if( self->myClass->superClass->items.pBase[i]->type == DAO_CLASS ){
      obj = (DaoObject*) DaoObject_MapThisObject ( 
          self->superObject->items.pObject[i], host, typer );
      if( obj && host == obj->myClass ) return (DaoBase*) obj;
    }else{
      if( DaoCData_ChildOf( self->myClass->superClass->items.pCData[i]->typer, typer ) ){
        return self->superObject->items.pBase[i];
      }
    }
  }
  return NULL;
}
DaoCData* DaoObject_MapCData( DaoObject *self, DaoTypeBase *typer )
{
  DaoBase *p = DaoObject_MapThisObject( self, NULL, typer );
  if( p && p->type == DAO_CDATA ) return (DaoCData*) p;
  return NULL;
}

void DaoObject_AddData( DaoObject *self, DString *name, DaoBase  *data )
{
}
int DaoObject_SetData( DaoObject *self, DString *name, DValue data, DaoObject *objThis )
{
  DaoAbsType *type;
  DValue *value ;
  DNode *node;
  int id, sto, perm;

  node = DMap_Find( self->myClass->lookupTable, name );
  if( node == NULL ) return DAO_ERROR_FIELD_NOTEXIST;

  perm = LOOKUP_PM( node->value.pInt );
  sto = LOOKUP_ST( node->value.pInt );
  id = LOOKUP_ID( node->value.pInt );
  if( objThis == self || perm == DAO_CLS_PUBLIC
    || (objThis && DaoObject_ChildOf( objThis, self ) && perm >= DAO_CLS_PROTECTED) ){
    if( sto == DAO_CLASS_VARIABLE ){
      if( id <0 ) return DAO_ERROR_FIELD_NOTPERMIT;
      type = self->myClass->objDataType->items.pAbtp[ id ];
      value = self->objValues + id;
      DValue_Move( data, value, type );
    }else if( sto == DAO_CLASS_GLOBAL ){
      value = self->myClass->glbData->data + id;
      type = self->myClass->glbDataType->items.pAbtp[ id ];
      DValue_Move( data, value, type );
    }
  }else{
    return DAO_ERROR_FIELD_NOTPERMIT;
  }
  return 0;
}
int DaoObject_GetData( DaoObject *self, DString *name, DValue *data, DaoObject *objThis, DValue **d2 )
{
  DValue *p = NULL;
  DNode *node;
  int id, sto, perm;

  *data = daoNilValue;
  node = DMap_Find( self->myClass->lookupTable, name );
  if( node == NULL ) return DAO_ERROR_FIELD_NOTEXIST;
  
  perm = LOOKUP_PM( node->value.pInt );
  sto = LOOKUP_ST( node->value.pInt );
  id = LOOKUP_ID( node->value.pInt );
  if( objThis == self || perm == DAO_CLS_PUBLIC 
    || (objThis && DaoObject_ChildOf( objThis, self ) && perm >= DAO_CLS_PROTECTED) ){
    switch( sto ){
    case DAO_CLASS_VARIABLE : p = self->objValues + id; break;
    case DAO_CLASS_GLOBAL   : p = self->myClass->glbData->data + id; break;
    case DAO_CLASS_CONST    : p = self->myClass->cstData->data + id; break;
    default : break;
    }
    if( p ) *data = *p;
    if( d2 ) *d2 = p;
  }else{
    return DAO_ERROR_FIELD_NOTPERMIT;
  }
  return 0;
}

DValue DaoObject_GetField( DaoObject *self, const char *name )
{
  int len = strlen( name );
  DValue res = daoNilValue;
  DString str = { 0, 0, NULL, NULL, NULL };
  str.size = str.bufSize = len;
  str.mbs = (char*) name;
  str.data = (size_t*) name;
#if 0
  DString *s = DString_New(1);
  DString_SetMBS( s, name );
  DaoObject_GetData( self, s, & res, self, NULL );
  DString_Delete( s );
#else
  DaoObject_GetData( self, & str, & res, self, NULL );
#endif
  return res;
}