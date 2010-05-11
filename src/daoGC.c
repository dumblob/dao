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

#include"daoGC.h"
#include"daoMap.h"
#include"daoClass.h"
#include"daoObject.h"
#include"daoNumeric.h"
#include"daoContext.h"
#include"daoProcess.h"
#include"daoRoutine.h"
#include"daoNamespace.h"
#include"daoThread.h"


#define GC_IN_POOL 1
#define GC_MARKED  2

#if 0
#define DEBUG_TRACE
#endif

#ifdef DEBUG_TRACE
#include <execinfo.h>

void print_trace()
{
  void  *array[100];
  size_t i, size = backtrace (array, 100);
  char **strings = backtrace_symbols (array, size);
  printf ("===========================================\n");
  printf ("Obtained %zd stack frames.\n", size);
  for (i = 0; i < size; i++) printf ("%s\n", strings[i]);
  free (strings);
}

#endif

static void DaoGC_DecRC2( DaoBase *p, int change );
static void cycRefCountDecrement( DaoBase *dbase );
static void cycRefCountIncrement( DaoBase *dbase );
static void cycRefCountDecrements( DArray * dbases );
static void cycRefCountIncrements( DArray * dbases );
static void directRefCountDecrement( DArray * dbases );
static void cycRefCountDecrementV( DValue value );
static void cycRefCountIncrementV( DValue value );
static void cycRefCountDecrementsV( DVarray * values );
static void cycRefCountIncrementsV( DVarray * values );
static void directRefCountDecrementV( DVarray * values );
static void cycRefCountDecrementsT( DPtrTuple * values );
static void cycRefCountIncrementsT( DPtrTuple * values );
static void directRefCountDecrementT( DPtrTuple * values );
static void cycRefCountDecrementsVT( DVaTuple * values );
static void cycRefCountIncrementsVT( DVaTuple * values );
static void directRefCountDecrementVT( DVaTuple * values );
static void cycRefCountDecreScan();
static void cycRefCountIncreScan();
static void freeGarbage();
static void InitGC();

#ifdef DAO_WITH_THREAD
static void markAliveObjects( DaoBase *root );
static void recycleGarbage( void * );
static void tryInvoke();
#endif


struct DaoGarbageCollector
{
  DArray   *pool[2];
  DArray   *objAlive;

  short     work, idle;
  int       gcMin, gcMax;
  int       count, boundary;
  int       ii, jj;
  short     busy;
  short     workType;
  short     finalizing;

#ifdef DAO_WITH_THREAD
  DThread   thread;

  DMutex    mutex_switch_heap;
  DMutex    mutex_start_gc;
  DMutex    mutex_block_mutator;

  DCondVar  condv_start_gc;
  DCondVar  condv_block_mutator;
#endif
};
static DaoGarbageCollector gcWorker = { { NULL, NULL }, NULL };

extern int ObjectProfile[100];
extern int daoCountMBS;
extern int daoCountArray;

int DaoGC_Min( int n /*=-1*/ )
{
  int prev = gcWorker.gcMin;
  if( n > 0 ) gcWorker.gcMin = n;
  return prev;
}
int DaoGC_Max( int n /*=-1*/ )
{
  int prev = gcWorker.gcMax;
  if( n > 0 ) gcWorker.gcMax = n;
  return prev;
}

void InitGC()
{
  if( gcWorker.objAlive != NULL ) return;

  gcWorker.pool[0] = DArray_New(0);
  gcWorker.pool[1] = DArray_New(0);
  gcWorker.objAlive = DArray_New(0);
  
  gcWorker.idle = 0;
  gcWorker.work = 1;
  gcWorker.finalizing = 0;

  gcWorker.gcMin = 1000;
  gcWorker.gcMax = 100 * gcWorker.gcMin;
  gcWorker.count = 0;
  gcWorker.workType = 0;
  gcWorker.ii = 0;
  gcWorker.jj = 0;
  gcWorker.busy = 0;

#ifdef DAO_WITH_THREAD
  DThread_Init( & gcWorker.thread );
  DMutex_Init( & gcWorker.mutex_switch_heap );
  DMutex_Init( & gcWorker.mutex_start_gc );
  DMutex_Init( & gcWorker.mutex_block_mutator );
  DCondVar_Init( & gcWorker.condv_start_gc );
  DCondVar_Init( & gcWorker.condv_block_mutator );
#endif
}
void DaoStartGC()
{
  InitGC();
#ifdef DAO_WITH_THREAD
  DThread_Start( & gcWorker.thread, recycleGarbage, NULL );
#endif
}
static int DValue_Size( DValue *self, int depth )
{
  DMap *map;
  DNode *it;
  int i, m, n = 0;
  switch( self->t ){
  case DAO_NIL :
  case DAO_INTEGER :
  case DAO_FLOAT :
  case DAO_DOUBLE :
    n = 1;
    break;
  case DAO_LONG :
    n = self->v.l->size;
    break;
  case DAO_COMPLEX :
    n = 2;
    break;
  case DAO_STRING :
    n = DString_Size( self->v.s );
    break;
  case DAO_ARRAY :
    n = self->v.array->size;
    break;
  case DAO_LIST :
    n = m = self->v.list->items->size;
    if( (-- depth) <=0 ) break;
    for(i=0; i<m; i++){
      n += DValue_Size( self->v.list->items->data + i, depth );
      if( n > gcWorker.gcMin ) break;
    }
    break;
  case DAO_TUPLE :
    n = m = self->v.tuple->items->size;
    if( (-- depth) <=0 ) break;
    for(i=0; i<m; i++){
      n += DValue_Size( self->v.tuple->items->data + i, depth );
      if( n > gcWorker.gcMin ) break;
    }
    break;
  case DAO_MAP :
    map = self->v.map->items;
    n = map->size;
    if( (-- depth) <=0 ) break;
    for(it=DMap_First( map ); it; it=DMap_Next( map, it ) ){
      n += DValue_Size( it->key.pValue, depth );
      n += DValue_Size( it->value.pValue, depth );
      if( n > gcWorker.gcMin ) break;
    }
    break;
  case DAO_OBJECT :
    if( self->v.object->objData == NULL ) break;
    n = m = self->v.object->objData->size;
    if( (-- depth) <=0 ) break;
    for(i=1; i<m; i++){ /* skip self object */
      n += DValue_Size( self->v.object->objValues + i, depth );
      if( n > gcWorker.gcMin ) break;
    }
    break;
  case DAO_CONTEXT :
    n = m = self->v.context->regArray->size;
    if( (-- depth) <=0 ) break;
    for(i=0; i<m; i++){
      n += DValue_Size( self->v.context->regArray->data + i, depth );
      if( n > gcWorker.gcMin ) break;
    }
    break;
  default : n = 1; break;
  }
  return n;
}
static void DaoGC_DecRC2( DaoBase *p, int change )
{
  DaoAbsType *tp, *tp2;
  DNode *node;
  const short idle = gcWorker.idle;
  int i, n;
  p->refCount += change;

  if( p->refCount == 0 ){
    DValue value = daoNilValue;
    value.t = p->type;
    value.v.p = p;
    /* free some memory, but do not delete it here,
     * because it may be a member of DValue,
     * and the DValue.t is not yet set to zero. */
    switch( p->type ){
    case DAO_LIST :
      {
        DaoList *list = (DaoList*) p;
        n = list->items->size;
        if( list->unitype && list->unitype->nested->size ){
          tp = list->unitype->nested->items.pAbtp[0];
          for(i=0; i<n; i++){
            DValue *it = list->items->data + i;
            if( it->t > DAO_DOUBLE && it->t < DAO_ARRAY ) DValue_Clear( it );
          }
        }
        break;
      }
    case DAO_TUPLE :
      {
        DaoTuple *tuple = (DaoTuple*) p;
        n = tuple->items->size;
        for(i=0; i<n; i++){
          DValue *it = tuple->items->data + i;
          if( it->t > DAO_DOUBLE && it->t < DAO_ARRAY ) DValue_Clear( it );
        }
        break;
      }
    case DAO_MAP :
      {
        DaoMap *map = (DaoMap*) p;
        tp2 = map->unitype;
        if( tp2 == NULL || tp2->nested->size != 2 ) break;
        tp = tp2->nested->items.pAbtp[0];
        tp2 = tp2->nested->items.pAbtp[1];
        if( tp->tid >DAO_DOUBLE && tp2->tid >DAO_DOUBLE && tp->tid <DAO_ARRAY && tp2->tid <DAO_ARRAY ){
          /* DaoMap_Clear( map ); this is NOT thread safe, there is RC update scan */
          i = 0;
          node = DMap_First( map->items );
          for( ; node != NULL; node = DMap_Next( map->items, node ) ) {
            DValue_Clear( node->key.pValue );
            DValue_Clear( node->value.pValue );
            node->key.pValue->t = DAO_INTEGER;
            node->key.pValue->v.i = ++ i; /* keep key ordering */
          }
        }else if( tp2->tid >DAO_DOUBLE && tp2->tid < DAO_ARRAY ){
          node = DMap_First( map->items );
          for( ; node != NULL; node = DMap_Next( map->items, node ) ) {
            DValue_Clear( node->value.pValue );
          }
        }else if( tp->tid >DAO_DOUBLE && tp->tid < DAO_ARRAY ){
          i = 0;
          node = DMap_First( map->items );
          for( ; node != NULL; node = DMap_Next( map->items, node ) ) {
            DValue_Clear( node->key.pValue );
            node->key.pValue->t = DAO_INTEGER;
            node->key.pValue->v.i = ++ i; /* keep key ordering */
          }
        }
        break;
      }
    case DAO_ARRAY :
      {
#ifdef DAO_WITH_NUMARRAY
        DaoArray *array = (DaoArray*) p;
        DaoArray_ResizeVector( array, 0 );
#endif
        break;
      }
#if 0
    case DAO_OBJECT :
      {
        DaoObject *object = (DaoObject*) p;
        if( object->objData == NULL ) break;
        n = object->objData->size;
        m = 0;
        for(i=0; i<n; i++){
          DValue *it = object->objValues + i;
          if( it->t && it->t < DAO_ARRAY ){
            DValue_Clear( it );
            m ++;
          }
        }
        if( m == n ) DVaTuple_Clear( object->objData );
        break;
      }
#endif
    case DAO_CONTEXT :
      {
        DaoContext *ctx = (DaoContext*) p;
        if( ctx->regValues ) dao_free( ctx->regValues );
        ctx->regValues = NULL;
        n = ctx->regArray->size;
        for(i=0; i<n; i++){
          DValue *it = ctx->regArray->data + i;
          if( it->t > DAO_DOUBLE && it->t < DAO_ARRAY ) DValue_Clear( it );
        }
        break;
      }
    default : break;
    }
#if 0
    gcWorker.count += DValue_Size( & value, 100 );
#endif
  }
  /* already in idle pool */
  if( p->gcState[ idle ] & GC_IN_POOL ) return;
  p->gcState[ idle ] = GC_IN_POOL;
  DArray_Append( gcWorker.pool[ idle ], p );
#if 0
  if( p->refCount ==0 ) return; /* already counted */
#endif
  switch( p->type ){
    case DAO_LIST :
      {
        DaoList *list = (DaoList*) p;
        gcWorker.count += list->items->size + 1;
        break;
      }
    case DAO_MAP :
      {
        DaoMap *map = (DaoMap*) p;
        gcWorker.count += map->items->size + 1;
        break;
      }
    case DAO_OBJECT :
      {
        DaoObject *obj = (DaoObject*) p;
        if( obj->objData ) gcWorker.count += obj->objData->size + 1;
        break;
      }
    case DAO_ARRAY :
      {
#ifdef DAO_WITH_NUMARRAY
        DaoArray *array = (DaoArray*) p;
        gcWorker.count += array->size + 1;
#endif
        break;
      }
    case DAO_TUPLE :
      {
        DaoTuple *tuple = (DaoTuple*) p;
        gcWorker.count += tuple->items->size;
        break;
      }
    case DAO_CONTEXT :
      {
        DaoContext *ctx = (DaoContext*) p;
        gcWorker.count += ctx->regArray->size;
        break;
      }
    default: gcWorker.count += 1; break;
  }
}
#ifdef DAO_WITH_THREAD

/* Concurrent Garbage Collector */

void DaoGC_DecRC( DaoBase *p )
{
  const short idle = gcWorker.idle;
  DaoTypeBase *typer;
  if( ! p ) return;

  DMutex_Lock( & gcWorker.mutex_switch_heap );

  DaoGC_DecRC2( p, -1 );

  DMutex_Unlock( & gcWorker.mutex_switch_heap );

  if( gcWorker.pool[ idle ]->size > gcWorker.gcMin ) tryInvoke();
}
void DaoFinishGC()
{
  gcWorker.gcMin = 0;
  gcWorker.finalizing = 1;
  tryInvoke();
  DThread_Join( & gcWorker.thread );

  DArray_Delete( gcWorker.pool[0] );
  DArray_Delete( gcWorker.pool[1] );
  DArray_Delete( gcWorker.objAlive );
}
void DaoGC_IncRC( DaoBase *p )
{
  if( ! p ) return;
  if( p->refCount == 0 ){
    p->refCount ++;
    return;
  }

  DMutex_Lock( & gcWorker.mutex_switch_heap );
  p->refCount ++;
  p->cycRefCount ++;
  DMutex_Unlock( & gcWorker.mutex_switch_heap );
}
void DaoGC_ShiftRC( DaoBase *up, DaoBase *down )
{
  const short idle = gcWorker.idle;
  if( up == down ) return;

  DMutex_Lock( & gcWorker.mutex_switch_heap );

  if( up ){
    up->refCount ++;
    up->cycRefCount ++;
  }
  if( down ) DaoGC_DecRC2( down, -1 );

  DMutex_Unlock( & gcWorker.mutex_switch_heap );

  if( down && gcWorker.pool[ idle ]->size > gcWorker.gcMin ) tryInvoke();
}

void DaoGC_IncRCs( DArray *list )
{
  int i;
  DaoBase **dbases;

  if( list->size == 0 ) return;
  dbases = list->items.pBase;
  DMutex_Lock( & gcWorker.mutex_switch_heap );
  for( i=0; i<list->size; i++){
    if( dbases[i] ){
      dbases[i]->refCount ++;
      dbases[i]->cycRefCount ++;
    }
  }
  DMutex_Unlock( & gcWorker.mutex_switch_heap );
}
void DaoGC_DecRCs( DArray *list )
{
  int i;
  DaoBase **dbases;
  const short idle = gcWorker.idle;
  if( list==NULL || list->size == 0 ) return;
  dbases = list->items.pBase;
  DMutex_Lock( & gcWorker.mutex_switch_heap );
  for( i=0; i<list->size; i++) DaoGC_DecRC2( dbases[i], -1 );
  DMutex_Unlock( & gcWorker.mutex_switch_heap );
  if( gcWorker.pool[ idle ]->size > gcWorker.gcMin ) tryInvoke();
}

void tryInvoke()
{
  DMutex_Lock( & gcWorker.mutex_start_gc );
  if( gcWorker.count >= gcWorker.gcMin ) DCondVar_Signal( & gcWorker.condv_start_gc );
  DMutex_Unlock( & gcWorker.mutex_start_gc );

  DMutex_Lock( & gcWorker.mutex_block_mutator );
  if( gcWorker.count >= gcWorker.gcMax ){
    DThreadData *thdData = DThread_GetSpecific();
    if( thdData && ! ( thdData->state & DTHREAD_NO_PAUSE ) )
      DCondVar_TimedWait( & gcWorker.condv_block_mutator, & gcWorker.mutex_block_mutator, 0.001 );
  }
  DMutex_Unlock( & gcWorker.mutex_block_mutator );
}

void recycleGarbage( void *p )
{
  while(1){
    if( gcWorker.finalizing && (gcWorker.pool[0]->size + gcWorker.pool[1]->size) ==0 ) break;
    if( ! gcWorker.finalizing ){
      DMutex_Lock( & gcWorker.mutex_block_mutator );
      DCondVar_BroadCast( & gcWorker.condv_block_mutator );
      DMutex_Unlock( & gcWorker.mutex_block_mutator );

      DMutex_Lock( & gcWorker.mutex_start_gc );
      DCondVar_TimedWait( & gcWorker.condv_start_gc, & gcWorker.mutex_start_gc, 0.1 );
      DMutex_Unlock( & gcWorker.mutex_start_gc );
      if( gcWorker.count < gcWorker.gcMin ) continue;
    }

    DMutex_Lock( & gcWorker.mutex_switch_heap );
    gcWorker.count = 0;
    gcWorker.work = gcWorker.idle;
    gcWorker.idle = ! gcWorker.work;
    DMutex_Unlock( & gcWorker.mutex_switch_heap );

    cycRefCountDecreScan();
    cycRefCountIncreScan();
    freeGarbage();

  }
  DThread_Exit( & gcWorker.thread );
}

void cycRefCountDecreScan()
{
  DArray *pool = gcWorker.pool[ gcWorker.work ];
  DNode *node;
  int i;
  for( i=0; i<pool->size; i++ )
    pool->items.pBase[i]->cycRefCount = pool->items.pBase[i]->refCount;

  for( i=0; i<pool->size; i++ ){
    DaoBase *dbase = pool->items.pBase[i];
    switch( dbase->type ){
#ifdef DAO_WITH_NUMARRAY
      case DAO_ARRAY :
        {
          DaoArray *array = (DaoArray*) dbase;
          cycRefCountDecrement( (DaoBase*) array->unitype );
          break;
        }
#endif
      case DAO_TUPLE :
        {
          DaoTuple *tuple = (DaoTuple*) dbase;
          cycRefCountDecrement( (DaoBase*) tuple->unitype );
          cycRefCountDecrementsVT( tuple->items );
          break;
        }
      case DAO_LIST :
        {
          DaoList *list = (DaoList*) dbase;
          cycRefCountDecrement( (DaoBase*) list->unitype );
          cycRefCountDecrementsV( list->items );
          break;
        }
      case DAO_MAP :
        {
          DaoMap *map = (DaoMap*) dbase;
          cycRefCountDecrement( (DaoBase*) map->unitype );
          node = DMap_First( map->items );
          for( ; node != NULL; node = DMap_Next( map->items, node ) ) {
            cycRefCountDecrementV( node->key.pValue[0] );
            cycRefCountDecrementV( node->value.pValue[0] );
          }
          break;
        }
      case DAO_OBJECT :
        {
          DaoObject *obj = (DaoObject*) dbase;
          cycRefCountDecrementsT( obj->superObject );
          cycRefCountDecrementsVT( obj->objData );
          break;
        }
      case DAO_ROUTINE :
      case DAO_FUNCTION :
        {
          DaoRoutine *rout = (DaoRoutine*)dbase;
          cycRefCountDecrement( (DaoBase*) rout->routType );
          cycRefCountDecrement( (DaoBase*) rout->nameSpace );
          cycRefCountDecrementsV( rout->routConsts );
          cycRefCountDecrements( rout->routOverLoad );
          if( dbase->type == DAO_ROUTINE )
            cycRefCountDecrements( rout->regType );
          break;
        }
      case DAO_CLASS :
        {
          DaoClass *klass = (DaoClass*)dbase;
          node = DMap_First( klass->abstypes );
          for( ; node != NULL; node = DMap_Next( klass->abstypes, node ) )
            cycRefCountDecrement( node->value.pBase );
          cycRefCountDecrement( (DaoBase*) klass->clsType );
          cycRefCountDecrementsV( klass->cstData );
          cycRefCountDecrementsV( klass->glbData );
          cycRefCountDecrements( klass->superClass );
          break;
        }
      case DAO_CONTEXT :
        {
          DaoContext *ctx = (DaoContext*)dbase;
          cycRefCountDecrement( (DaoBase*) ctx->object );
          cycRefCountDecrement( (DaoBase*) ctx->routine );
          cycRefCountDecrementsVT( ctx->regArray );
          break;
        }
      case DAO_NAMESPACE :
        {
          DaoNameSpace *ns = (DaoNameSpace*) dbase;
          cycRefCountDecrementsV( ns->cstData );
          cycRefCountDecrementsV( ns->varData );
          cycRefCountDecrements( ns->cmethods );
          break;
        }
      case DAO_ABSTYPE :
        {
          DaoAbsType *abtp = (DaoAbsType*) dbase;
          cycRefCountDecrement( abtp->X.extra );
          cycRefCountDecrements( abtp->nested );
          break;
        }
      case DAO_VMPROCESS :
        {
          DaoVmProcess *vmp = (DaoVmProcess*) dbase;
          DaoVmFrame *frame = vmp->firstFrame;
          cycRefCountDecrementV( vmp->returned );
          cycRefCountDecrementsV( vmp->parResume );
          cycRefCountDecrementsV( vmp->parYield );
          cycRefCountDecrementsV( vmp->exceptions );
          while( frame ){
            cycRefCountDecrement( (DaoBase*) frame->context );
            frame = frame->next;
          }
          break;
        }
      default: break;
    }
  }
}
void cycRefCountIncreScan()
{
  const short work = gcWorker.work;
  DArray *pool = gcWorker.pool[ gcWorker.work ];
  int i, j;

  for(j=0; j<2; j++){
    for( i=0; i<pool->size; i++ ){
      DaoBase *dbase = pool->items.pBase[i];
      if( ( dbase->cycRefCount>0 && ! ( dbase->gcState[work] & GC_MARKED ) ) )
        markAliveObjects( dbase );
    }
  }
}
void markAliveObjects( DaoBase *root )
{
  const short work = gcWorker.work;
  DNode *node;
  int i;
  DArray *objAlive = gcWorker.objAlive;
  DArray_Clear( objAlive );
  root->gcState[work] |= GC_MARKED;
  DArray_Append( objAlive, root );

  for( i=0; i<objAlive->size; i++){
    DaoBase *dbase = objAlive->items.pBase[i];
    switch( dbase->type ){
#ifdef DAO_WITH_NUMARRAY
      case DAO_ARRAY :
        {
          DaoArray *array = (DaoArray*) dbase;
          cycRefCountIncrement( (DaoBase*) array->unitype );
          break;
        }
#endif
      case DAO_TUPLE :
        {
          DaoTuple *tuple= (DaoTuple*) dbase;
          cycRefCountIncrement( (DaoBase*) tuple->unitype );
          cycRefCountIncrementsVT( tuple->items );
          break;
        }
      case DAO_LIST :
        {
          DaoList *list= (DaoList*) dbase;
          cycRefCountIncrement( (DaoBase*) list->unitype );
          cycRefCountIncrementsV( list->items );
          break;
        }
      case DAO_MAP :
        {
          DaoMap *map = (DaoMap*)dbase;
          cycRefCountIncrement( (DaoBase*) map->unitype );
          node = DMap_First( map->items );
          for( ; node != NULL; node = DMap_Next( map->items, node ) ){
            cycRefCountIncrementV( node->key.pValue[0] );
            cycRefCountIncrementV( node->value.pValue[0] );
          }
          break;
        }
      case DAO_OBJECT :
        {
          DaoObject *obj = (DaoObject*) dbase;
          cycRefCountIncrementsT( obj->superObject );
          cycRefCountIncrementsVT( obj->objData );
          break;
        }
      case DAO_ROUTINE :
      case DAO_FUNCTION :
        {
          DaoRoutine *rout = (DaoRoutine*) dbase;
          cycRefCountIncrement( (DaoBase*) rout->routType );
          cycRefCountIncrement( (DaoBase*) rout->nameSpace );
          cycRefCountIncrementsV( rout->routConsts );
          cycRefCountIncrements( rout->routOverLoad );
          if( dbase->type == DAO_ROUTINE )
            cycRefCountIncrements( rout->regType );
          break;
        }
      case DAO_CLASS :
        {
          DaoClass *klass = (DaoClass*) dbase;
          node = DMap_First( klass->abstypes );
          for( ; node != NULL; node = DMap_Next( klass->abstypes, node ) )
            cycRefCountIncrement( node->value.pBase );
          cycRefCountIncrement( (DaoBase*) klass->clsType );
          cycRefCountIncrementsV( klass->cstData );
          cycRefCountIncrementsV( klass->glbData );
          cycRefCountIncrements( klass->superClass );
          break;
        }
      case DAO_CONTEXT :
        {
          DaoContext *ctx = (DaoContext*)dbase;
          cycRefCountIncrement( (DaoBase*) ctx->object );
          cycRefCountIncrement( (DaoBase*) ctx->routine );
          cycRefCountIncrementsVT( ctx->regArray );
          break;
        }
      case DAO_NAMESPACE :
        {
          DaoNameSpace *ns = (DaoNameSpace*) dbase;
          cycRefCountIncrementsV( ns->cstData );
          cycRefCountIncrementsV( ns->varData );
          cycRefCountIncrements( ns->cmethods );
          break;
        }
      case DAO_ABSTYPE :
        {
          DaoAbsType *abtp = (DaoAbsType*) dbase;
          cycRefCountIncrement( abtp->X.extra );
          cycRefCountIncrements( abtp->nested );
          break;
        }
      case DAO_VMPROCESS :
        {
          DaoVmProcess *vmp = (DaoVmProcess*) dbase;
          DaoVmFrame *frame = vmp->firstFrame;
          cycRefCountIncrementV( vmp->returned );
          cycRefCountIncrementsV( vmp->parResume );
          cycRefCountIncrementsV( vmp->parYield );
          cycRefCountIncrementsV( vmp->exceptions );
          while( frame ){
            cycRefCountIncrement( (DaoBase*) frame->context );
            frame = frame->next;
          }
          break;
        }
      default: break;
    }
  }
}

void freeGarbage()
{
  DaoTypeBase *typer;
  DArray *pool = gcWorker.pool[ gcWorker.work ];
  DNode *node;
  int i;
  const short work = gcWorker.work;
  const short idle = gcWorker.idle;

  for( i=0; i<pool->size; i++ ){
    DaoBase *dbase = pool->items.pBase[i];
    dbase->gcState[work] = 0;

    if( dbase->cycRefCount == 0 ){

      DMutex_Lock( & gcWorker.mutex_switch_heap );
      switch( dbase->type ){

#ifdef DAO_WITH_NUMARRAY
        case DAO_ARRAY :
          {
            DaoArray *array = (DaoArray*) dbase;
            if( array->unitype ){
              array->unitype->refCount --;
              array->unitype = NULL;
            }
            break;
          }
#endif
        case DAO_TUPLE :
          {
            DaoTuple *tuple = (DaoTuple*) dbase;
            directRefCountDecrementVT( tuple->items );
            if( tuple->unitype ){
              tuple->unitype->refCount --;
              tuple->unitype = NULL;
            }
            break;
          }
        case DAO_LIST :
          {
            DaoList *list = (DaoList*) dbase;
            directRefCountDecrementV( list->items );
            if( list->unitype ){
              list->unitype->refCount --;
              list->unitype = NULL;
            }
            break;
          }
        case DAO_MAP :
          {
            DaoMap *map = (DaoMap*) dbase;
            node = DMap_First( map->items );
            for( ; node != NULL; node = DMap_Next( map->items, node ) ){
              if( node->key.pValue->t >= DAO_ARRAY ){
                node->key.pValue->v.p->refCount --;
                node->key.pValue->t = 0;
              }else{
                DValue_Clear( node->key.pValue );
              }
              if( node->value.pValue->t >= DAO_ARRAY ){
                node->value.pValue->v.p->refCount --;
                node->value.pValue->t = 0;
              }else{
                DValue_Clear( node->value.pValue );
              }
            }
            DMap_Clear( map->items );
            if( map->unitype ){
              map->unitype->refCount --;
              map->unitype = NULL;
            }
            break;
          }
        case DAO_OBJECT :
          {
            DaoObject *obj = (DaoObject*) dbase;
            directRefCountDecrementT( obj->superObject );
            directRefCountDecrementVT( obj->objData );
            break;
          }
        case DAO_ROUTINE :
        case DAO_FUNCTION :
          {
            DaoRoutine *rout = (DaoRoutine*)dbase;
            if( rout->nameSpace ) rout->nameSpace->refCount --;
            rout->nameSpace = 0;
            if( rout->routType ){
              rout->routType->refCount --;
              rout->routType = NULL;
            }
            directRefCountDecrementV( rout->routConsts );
            directRefCountDecrement( rout->routOverLoad );
            if( dbase->type == DAO_ROUTINE )
              directRefCountDecrement( rout->regType );
            break;
          }
        case DAO_CLASS :
          {
            DaoClass *klass = (DaoClass*)dbase;
            klass->clsType->refCount --;
            klass->clsType = 0;
            node = DMap_First( klass->abstypes );
            for( ; node != NULL; node = DMap_Next( klass->abstypes, node ) )
              node->value.pBase->refCount --;
            DMap_Clear( klass->abstypes );
            directRefCountDecrementV( klass->cstData );
            directRefCountDecrementV( klass->glbData );
            directRefCountDecrement( klass->superClass );
            break;
          }
        case DAO_CONTEXT :
          {
            DaoContext *ctx = (DaoContext*)dbase;
            if( ctx->object ) ctx->object->refCount --;
            if( ctx->routine ) ctx->routine->refCount --;
            ctx->object = 0;
            ctx->routine = 0;
            directRefCountDecrementVT( ctx->regArray );
            break;
          }
        case DAO_NAMESPACE :
          {
            DaoNameSpace *ns = (DaoNameSpace*) dbase;
            directRefCountDecrementV( ns->cstData );
            directRefCountDecrementV( ns->varData );
            directRefCountDecrement( ns->cmethods );
            break;
          }
        case DAO_ABSTYPE :
          {
            DaoAbsType *abtp = (DaoAbsType*) dbase;
            directRefCountDecrement( abtp->nested );
            if( abtp->X.extra ){
              abtp->X.extra->refCount --;
              abtp->X.extra = NULL;
            }
            break;
          }
        case DAO_VMPROCESS :
          {
            DaoVmProcess *vmp = (DaoVmProcess*) dbase;
            DaoVmFrame *frame = vmp->firstFrame;
            if( vmp->returned.t >= DAO_ARRAY ) vmp->returned.v.p->refCount --;
            vmp->returned.t = 0;
            directRefCountDecrementV( vmp->parResume );
            directRefCountDecrementV( vmp->parYield );
            directRefCountDecrementV( vmp->exceptions );
            while( frame ){
              if( frame->context ) frame->context->refCount --;
              frame->context = NULL;
              frame = frame->next;
            }
            break;
          }
        default: break;
      }
      DMutex_Unlock( & gcWorker.mutex_switch_heap );
    }
  }

  for( i=0; i<pool->size; i++ ){
    DaoBase *dbase = pool->items.pBase[i];
    if( dbase->cycRefCount==0 ){
      if( dbase->refCount !=0 ){
        printf(" refCount not zero %i: %i\n", dbase->type, dbase->refCount );

        DMutex_Lock( & gcWorker.mutex_switch_heap );
        if( ! ( dbase->gcState[ idle ] & GC_IN_POOL ) ){
          dbase->gcState[ idle ] = GC_IN_POOL;
          DArray_Append( gcWorker.pool[idle], dbase );
        }
        DMutex_Unlock( & gcWorker.mutex_switch_heap );
        continue;
      }
      if( ! ( dbase->gcState[idle] & GC_IN_POOL ) ){
#ifdef DAO_GC_PROF
        ObjectProfile[dbase->type] --;
#endif
        /*
        if( dbase->type <= DAO_VMPROCESS )
        if( dbase->type == DAO_STRING ){
          DaoString *s = (DaoString*) dbase;
          if( s->chars->mbs && s->chars->mbs->refCount > 1 ){
            printf( "delete mbstring!!! %i\n", s->chars->mbs->refCount );
          }
          if( s->chars->wcs && s->chars->wcs->refCount > 1 ){
            printf( "delete wcstring!!! %i\n", s->chars->wcs->refCount );
          }
        }
        if( dbase->type < DAO_STRING )
        if( dbase->type != DAO_CONTEXT )
        */
        typer = DaoBase_GetTyper( dbase );
        typer->Delete( dbase );
      }
    }
  }
#ifdef DAO_GC_PROF
#warning "-------------------- DAO_GC_PROF is turned on."
  printf("heap[idle] = %i;\theap[work] = %i\n", gcWorker.pool[ idle ]->size, gcWorker.pool[ work ]->size );
  printf("=======================================\n");
  printf( "mbs count = %i\n", daoCountMBS );
  printf( "array count = %i\n", daoCountArray );
  for(i=0; i<100; i++){
    if( ObjectProfile[i] != 0 ){
      printf( "type = %3i; rest = %5i\n", i, ObjectProfile[i] );
    }
  }
#endif
  DArray_Clear( pool );
}
void cycRefCountDecrement( DaoBase *dbase )
{
  const short work = gcWorker.work;
  if( !dbase ) return;
  if( ! ( dbase->gcState[work] & GC_IN_POOL ) ){
    DArray_Append( gcWorker.pool[work], dbase );
    dbase->gcState[work] = GC_IN_POOL;
    dbase->cycRefCount = dbase->refCount;
  }
  dbase->cycRefCount --;

  if( dbase->cycRefCount<0 ){
    /*
       printf("cycRefCount<0 : %i\n", dbase->type);
     */
    dbase->cycRefCount = 0;
  }
}
void cycRefCountIncrement( DaoBase *dbase )
{
  const short work = gcWorker.work;
  if( dbase ){
    dbase->cycRefCount++;
    if( ! ( dbase->gcState[work] & GC_MARKED ) ){
      dbase->gcState[work] |= GC_MARKED;
      DArray_Append( gcWorker.objAlive, dbase );
    }
  }
}

#else

/* Incremental Garbage Collector */
enum DaoGCWorkType
{
  GC_INIT_RC ,
  GC_DEC_RC ,
  GC_INC_RC ,
  GC_INC_RC2 ,
  GC_DIR_DEC_RC ,
  GC_FREE
};

static void InitRC();
static void directDecRC();
static void SwitchGC();
static void ContinueGC();

void DaoGC_IncRC( DaoBase *p )
{
  const short work = gcWorker.work;
  if( ! p ) return;
#ifdef DEBUG_TRACE
  //if( p == 0x15b5910 ) print_trace();
#endif
  if( p->refCount == 0 ){
    p->refCount ++;
    return;
  }

  p->refCount ++;
  p->cycRefCount ++;
  if( ! ( p->gcState[work] & GC_IN_POOL ) && gcWorker.workType == GC_INC_RC ){
    DArray_Append( gcWorker.pool[work], p );
    p->gcState[work] = GC_IN_POOL;
  }
}
static int counts = 100;
void DaoGC_DecRC( DaoBase *p )
{
  const short idle = gcWorker.idle;
  if( ! p ) return;

#if 0
  if( p->type == DAO_ABSTYPE ){
    DaoAbsType *abtp = (DaoAbsType*) p;
    if( abtp->tid == DAO_LIST )
    return;
  }

#include"assert.h"
  printf( "DaoGC_DecRC: %i\n", p->type );
  assert( p->type != 48 );
  printf( "here: %i %i\n", gcWorker.pool[ ! idle ]->size, gcWorker.pool[ idle ]->size );
#endif

  DaoGC_DecRC2( p, -1 );

  if( gcWorker.busy ) return;
  counts --;
  if( gcWorker.count < gcWorker.gcMax ){
    if( counts ) return;
    counts = 100;
  }else{
    if( counts ) return;
    counts = 10;
  }

  if( gcWorker.pool[ ! idle ]->size )
    ContinueGC();
  else if( gcWorker.pool[ idle ]->size > gcWorker.gcMin )
    SwitchGC();
}
void DaoGC_IncRCs( DArray *list )
{
  int i;
  DaoBase **data;
  if( list->size == 0 ) return;
  data = list->items.pBase;
  for( i=0; i<list->size; i++) DaoGC_IncRC( data[i] );
}
void DaoGC_DecRCs( DArray *list )
{
  int i;
  DaoBase **data;
  if( list == NULL || list->size == 0 ) return;
  data = list->items.pBase;
  for( i=0; i<list->size; i++) DaoGC_DecRC( data[i] );
}
void DaoGC_ShiftRC( DaoBase *up, DaoBase *down )
{
  if( up == down ) return;
  if( up ) DaoGC_IncRC( up );
  if( down ) DaoGC_DecRC( down );
}

void SwitchGC()
{
  if( gcWorker.busy ) return;
  gcWorker.work = gcWorker.idle;
  gcWorker.idle = ! gcWorker.work;
  gcWorker.workType = 0;
  gcWorker.ii = 0;
  gcWorker.jj = 0;
  ContinueGC();
}
void ContinueGC()
{
  if( gcWorker.busy ) return;
  gcWorker.busy = 1;
  switch( gcWorker.workType ){
    case GC_INIT_RC :
      InitRC();
      break;
    case GC_DEC_RC :
      cycRefCountDecreScan();
      break;
    case GC_INC_RC :
    case GC_INC_RC2 :
      cycRefCountIncreScan();
      break;
    case GC_DIR_DEC_RC :
      directDecRC();
      break;
    case GC_FREE :
      freeGarbage();
      break;
    default : break;
  }
  gcWorker.busy = 0;
}
void DaoFinishGC()
{
  short idle = gcWorker.idle;
  while( gcWorker.pool[ idle ]->size || gcWorker.pool[ ! idle ]->size ){
    while( gcWorker.pool[ ! idle ]->size ) ContinueGC();
    if( gcWorker.pool[ idle ]->size ) SwitchGC();
    idle = gcWorker.idle;
    while( gcWorker.pool[ ! idle ]->size ) ContinueGC();
  }

  DArray_Delete( gcWorker.pool[0] );
  DArray_Delete( gcWorker.pool[1] );
  DArray_Delete( gcWorker.objAlive );
}
void InitRC()
{
  DArray *pool = gcWorker.pool[ gcWorker.work ];
  int i = gcWorker.ii;
  int k = gcWorker.ii + gcWorker.gcMin / 2;
  for( ; i<pool->size; i++ ){
    pool->items.pBase[i]->cycRefCount = pool->items.pBase[i]->refCount;
    if( i > k ) break;
  }
  if( i >= pool->size ){
    gcWorker.ii = 0;
    gcWorker.workType = GC_DEC_RC;
  }else{
    gcWorker.ii = i+1;
  }
}
void cycRefCountDecreScan()
{
  DArray *pool = gcWorker.pool[ gcWorker.work ];
  DNode *node;
  int i = gcWorker.ii;
  int j = 0;

  for( ; i<pool->size; i++ ){
    DaoBase *dbase = pool->items.pBase[i];
    switch( dbase->type ){
#ifdef DAO_WITH_NUMARRAY
      case DAO_ARRAY :
        {
          DaoArray *array = (DaoArray*) dbase;
          cycRefCountDecrement( (DaoBase*) array->unitype );
          j ++;
          break;
        }
#endif
      case DAO_TUPLE :
        {
          DaoTuple *tuple = (DaoTuple*) dbase;
          cycRefCountDecrement( (DaoBase*) tuple->unitype );
          cycRefCountDecrementsVT( tuple->items );
          j += tuple->items->size;
          break;
        }
      case DAO_LIST :
        {
          DaoList *list = (DaoList*) dbase;
          cycRefCountDecrement( (DaoBase*) list->unitype );
          cycRefCountDecrementsV( list->items );
          j += list->items->size;
          break;
        }
      case DAO_MAP :
        {
          DaoMap *map = (DaoMap*) dbase;
          cycRefCountDecrement( (DaoBase*) map->unitype );
          node = DMap_First( map->items );
          for( ; node != NULL; node = DMap_Next( map->items, node ) ) {
            cycRefCountDecrementV( node->key.pValue[0] );
            cycRefCountDecrementV( node->value.pValue[0] );
          }
          j += map->items->size;
          break;
        }
      case DAO_OBJECT :
        {
          DaoObject *obj = (DaoObject*) dbase;
          cycRefCountDecrementsT( obj->superObject );
          cycRefCountDecrementsVT( obj->objData );
          if( obj->superObject ) j += obj->superObject->size;
          if( obj->objData ) j += obj->objData->size;
          break;
        }
      case DAO_ROUTINE :
      case DAO_FUNCTION :
        {
          DaoRoutine *rout = (DaoRoutine*)dbase;
          cycRefCountDecrement( (DaoBase*) rout->routType );
          cycRefCountDecrement( (DaoBase*) rout->nameSpace );
          cycRefCountDecrementsV( rout->routConsts );
          cycRefCountDecrements( rout->routOverLoad );
          j += rout->routConsts->size + rout->routOverLoad->size;
          if( dbase->type == DAO_ROUTINE ){
            j += rout->regType->size;
            cycRefCountDecrements( rout->regType );
          }
          break;
        }
      case DAO_CLASS :
        {
          DaoClass *klass = (DaoClass*)dbase;
          node = DMap_First( klass->abstypes );
          for( ; node != NULL; node = DMap_Next( klass->abstypes, node ) )
            cycRefCountDecrement( node->value.pBase );
          cycRefCountDecrement( (DaoBase*) klass->clsType );
          cycRefCountDecrementsV( klass->cstData );
          cycRefCountDecrementsV( klass->glbData );
          cycRefCountDecrements( klass->superClass );
          j += klass->cstData->size + klass->glbData->size;
          j += klass->superClass->size + klass->abstypes->size;
          break;
        }
      case DAO_CONTEXT :
        {
          DaoContext *ctx = (DaoContext*)dbase;
          cycRefCountDecrement( (DaoBase*) ctx->object );
          cycRefCountDecrement( (DaoBase*) ctx->routine );
          cycRefCountDecrementsVT( ctx->regArray );
          j += ctx->regArray->size + 3;
          break;
        }
      case DAO_NAMESPACE :
        {
          DaoNameSpace *ns = (DaoNameSpace*) dbase;
          cycRefCountDecrementsV( ns->cstData );
          cycRefCountDecrementsV( ns->varData );
          cycRefCountDecrements( ns->cmethods );
          j += ns->cstData->size + ns->varData->size;
          break;
        }
      case DAO_ABSTYPE :
        {
          DaoAbsType *abtp = (DaoAbsType*) dbase;
          cycRefCountDecrement( abtp->X.extra );
          cycRefCountDecrements( abtp->nested );
          break;
        }
      case DAO_VMPROCESS :
        {
          DaoVmProcess *vmp = (DaoVmProcess*) dbase;
          DaoVmFrame *frame = vmp->firstFrame;
          cycRefCountDecrementV( vmp->returned );
          cycRefCountDecrementsV( vmp->parResume );
          cycRefCountDecrementsV( vmp->parYield );
          cycRefCountDecrementsV( vmp->exceptions );
          while( frame ){
            cycRefCountDecrement( (DaoBase*) frame->context );
            frame = frame->next;
          }
          break;
        }
      default: break;
    }
    if( j >= gcWorker.gcMin ) break;
  }
  if( i >= pool->size ){
    gcWorker.ii = 0;
    gcWorker.workType = GC_INC_RC;
  }else{
    gcWorker.ii = i+1;
  }
}
void cycRefCountIncreScan()
{
  const short work = gcWorker.work;
  DArray *pool = gcWorker.pool[ gcWorker.work ];
  DNode *node;
  int i = gcWorker.ii;
  int j = 0;

  for( ; i<pool->size; i++ ){
    DaoBase *dbase = pool->items.pBase[i];
    j ++;
    if( dbase->cycRefCount >0 && ! ( dbase->gcState[work] & GC_MARKED ) ){
      dbase->gcState[work] |= GC_MARKED;
      switch( dbase->type ){
#ifdef DAO_WITH_NUMARRAY
        case DAO_ARRAY :
          {
            DaoArray *array = (DaoArray*) dbase;
            cycRefCountIncrement( (DaoBase*) array->unitype );
            break;
          }
#endif
        case DAO_TUPLE :
          {
            DaoTuple *tuple= (DaoTuple*) dbase;
            cycRefCountIncrement( (DaoBase*) tuple->unitype );
            cycRefCountIncrementsVT( tuple->items );
            j += tuple->items->size;
            break;
          }
        case DAO_LIST :
          {
            DaoList *list= (DaoList*) dbase;
            cycRefCountIncrement( (DaoBase*) list->unitype );
            cycRefCountIncrementsV( list->items );
            j += list->items->size;
            break;
          }
        case DAO_MAP :
          {
            DaoMap *map = (DaoMap*)dbase;
            cycRefCountIncrement( (DaoBase*) map->unitype );
            node = DMap_First( map->items );
            for( ; node != NULL; node = DMap_Next( map->items, node ) ){
              cycRefCountIncrementV( node->key.pValue[0] );
              cycRefCountIncrementV( node->value.pValue[0] );
            }
            j += map->items->size;
            break;
          }
        case DAO_OBJECT :
          {
            DaoObject *obj = (DaoObject*) dbase;
            cycRefCountIncrementsT( obj->superObject );
            cycRefCountIncrementsVT( obj->objData );
            if( obj->superObject ) j += obj->superObject->size;
            if( obj->objData ) j += obj->objData->size;
            break;
          }
        case DAO_ROUTINE :
        case DAO_FUNCTION :
          {
            DaoRoutine *rout = (DaoRoutine*) dbase;
            cycRefCountIncrement( (DaoBase*) rout->routType );
            cycRefCountIncrement( (DaoBase*) rout->nameSpace );
            cycRefCountIncrementsV( rout->routConsts );
            cycRefCountIncrements( rout->routOverLoad );
          if( dbase->type == DAO_ROUTINE ){
            cycRefCountIncrements( rout->regType );
          }
            j += rout->routConsts->size + rout->routOverLoad->size;
            break;
          }
        case DAO_CLASS :
          {
            DaoClass *klass = (DaoClass*) dbase;
            node = DMap_First( klass->abstypes );
            for( ; node != NULL; node = DMap_Next( klass->abstypes, node ) )
              cycRefCountIncrement( node->value.pBase );
            cycRefCountIncrement( (DaoBase*) klass->clsType );
            cycRefCountIncrementsV( klass->cstData );
            cycRefCountIncrementsV( klass->glbData );
            cycRefCountIncrements( klass->superClass );
            j += klass->cstData->size + klass->glbData->size;
            j += klass->superClass->size + klass->abstypes->size;
            break;
          }
        case DAO_CONTEXT :
          {
            DaoContext *ctx = (DaoContext*)dbase;
            cycRefCountIncrement( (DaoBase*) ctx->object );
            cycRefCountIncrement( (DaoBase*) ctx->routine );
            cycRefCountIncrementsVT( ctx->regArray );
            j += ctx->regArray->size + 3;
            break;
          }
        case DAO_NAMESPACE :
          {
            DaoNameSpace *ns = (DaoNameSpace*) dbase;
            cycRefCountIncrementsV( ns->cstData );
            cycRefCountIncrementsV( ns->varData );
            cycRefCountIncrements( ns->cmethods );
            j += ns->cstData->size + ns->varData->size;
            break;
          }
        case DAO_ABSTYPE :
          {
            DaoAbsType *abtp = (DaoAbsType*) dbase;
            cycRefCountIncrement( abtp->X.extra );
            cycRefCountIncrements( abtp->nested );
            break;
          }
        case DAO_VMPROCESS :
          {
            DaoVmProcess *vmp = (DaoVmProcess*) dbase;
            DaoVmFrame *frame = vmp->firstFrame;
            cycRefCountIncrementV( vmp->returned );
            cycRefCountIncrementsV( vmp->parResume );
            cycRefCountIncrementsV( vmp->parYield );
            cycRefCountIncrementsV( vmp->exceptions );
            while( frame ){
              cycRefCountIncrement( (DaoBase*) frame->context );
              frame = frame->next;
            }
            break;
          }
        default: break;
      }
    }
    if( j >= gcWorker.gcMin ) break;
  }
  if( i >= pool->size ){
    gcWorker.ii = 0;
    gcWorker.workType ++;
    gcWorker.boundary = pool->size;
  }else{
    gcWorker.ii = i+1;
  }
}
void directDecRC()
{
  DArray *pool = gcWorker.pool[ gcWorker.work ];
  DNode *node;
  const short work = gcWorker.work;
  int boundary = gcWorker.boundary;
  int i = gcWorker.ii;
  int j = 0;

  for( ; i<boundary; i++ ){
    DaoBase *dbase = pool->items.pBase[i];
    dbase->gcState[work] = 0;
    j ++;
    if( dbase->cycRefCount == 0 ){

      switch( dbase->type ){

#ifdef DAO_WITH_NUMARRAY
        case DAO_ARRAY :
          {
            DaoArray *array = (DaoArray*) dbase;
            if( array->unitype ){
              array->unitype->refCount --;
              array->unitype = NULL;
            }
            break;
          }
#endif
        case DAO_TUPLE :
          {
            DaoTuple *tuple = (DaoTuple*) dbase;
            j += tuple->items->size;
            directRefCountDecrementVT( tuple->items );
            if( tuple->unitype ){
              tuple->unitype->refCount --;
              tuple->unitype = NULL;
            }
            break;
          }
        case DAO_LIST :
          {
            DaoList *list = (DaoList*) dbase;
            j += list->items->size;
            directRefCountDecrementV( list->items );
            if( list->unitype ){
              list->unitype->refCount --;
              list->unitype = NULL;
            }
            break;
          }
        case DAO_MAP :
          {
            DaoMap *map = (DaoMap*) dbase;
            node = DMap_First( map->items );
            for( ; node != NULL; node = DMap_Next( map->items, node ) ){
              if( node->key.pValue->t >= DAO_ARRAY ){
                node->key.pValue->v.p->refCount --;
                node->key.pValue->t = 0;
              }else{
                DValue_Clear( node->key.pValue );
              }
              if( node->value.pValue->t >= DAO_ARRAY ){
                node->value.pValue->v.p->refCount --;
                node->value.pValue->t = 0;
              }else{
                DValue_Clear( node->value.pValue );
              }
            }
            j += map->items->size;
            DMap_Clear( map->items );
            if( map->unitype ){
              map->unitype->refCount --;
              map->unitype = NULL;
            }
            break;
          }
        case DAO_OBJECT :
          {
            DaoObject *obj = (DaoObject*) dbase;
            if( obj->superObject ) j += obj->superObject->size;
            if( obj->objData ) j += obj->objData->size;
            directRefCountDecrementT( obj->superObject );
            directRefCountDecrementVT( obj->objData );
            break;
          }
        case DAO_ROUTINE :
        case DAO_FUNCTION :
          {
            DaoRoutine *rout = (DaoRoutine*)dbase;
            if( rout->nameSpace ) rout->nameSpace->refCount --;
            rout->nameSpace = NULL;
            if( rout->routType ){
              /* may become NULL, if it has already become garbage 
               * in the last cycle */
              rout->routType->refCount --;
              rout->routType = NULL;
            }
            j += rout->routConsts->size + rout->routOverLoad->size;
            directRefCountDecrementV( rout->routConsts );
            directRefCountDecrement( rout->routOverLoad );
            if( dbase->type == DAO_ROUTINE ){
              directRefCountDecrement( rout->regType );
            }
            break;
          }
        case DAO_CLASS :
          {
            DaoClass *klass = (DaoClass*)dbase;
            j += klass->cstData->size + klass->glbData->size;
            j += klass->superClass->size + klass->abstypes->size;
            klass->clsType->refCount --;
            klass->clsType = 0;
            node = DMap_First( klass->abstypes );
            for( ; node != NULL; node = DMap_Next( klass->abstypes, node ) )
              node->value.pBase->refCount --;
            DMap_Clear( klass->abstypes );
            directRefCountDecrementV( klass->cstData );
            directRefCountDecrementV( klass->glbData );
            directRefCountDecrement( klass->superClass );
            break;
          }
        case DAO_CONTEXT :
          {
            DaoContext *ctx = (DaoContext*)dbase;
            if( ctx->object ) ctx->object->refCount --;
            if( ctx->routine ) ctx->routine->refCount --;
            ctx->object = 0;
            ctx->routine = 0;
            j += ctx->regArray->size + 3;
            directRefCountDecrementVT( ctx->regArray );
            break;
          }
        case DAO_NAMESPACE :
          {
            DaoNameSpace *ns = (DaoNameSpace*) dbase;
            j += ns->cstData->size + ns->varData->size;
            directRefCountDecrementV( ns->cstData );
            directRefCountDecrementV( ns->varData );
            directRefCountDecrement( ns->cmethods );
            break;
          }
        case DAO_ABSTYPE :
          {
            DaoAbsType *abtp = (DaoAbsType*) dbase;
            directRefCountDecrement( abtp->nested );
            if( abtp->X.extra ){
              abtp->X.extra->refCount --;
              abtp->X.extra = NULL;
            }
            break;
          }
        case DAO_VMPROCESS :
          {
            DaoVmProcess *vmp = (DaoVmProcess*) dbase;
            DaoVmFrame *frame = vmp->firstFrame;
            if( vmp->returned.t >= DAO_ARRAY ) vmp->returned.v.p->refCount --;
            vmp->returned.t = 0;
            directRefCountDecrementV( vmp->parResume );
            directRefCountDecrementV( vmp->parYield );
            directRefCountDecrementV( vmp->exceptions );
            while( frame ){
              if( frame->context ) frame->context->refCount --;
              frame->context = NULL;
              frame = frame->next;
            }
            break;
          }
        default: break;
      }
    }
    if( j >= gcWorker.gcMin ) break;
  }
  if( i >= boundary ){
    gcWorker.ii = 0;
    gcWorker.workType = GC_FREE;
  }else{
    gcWorker.ii = i+1;
  }
}

void freeGarbage()
{
  DArray *pool = gcWorker.pool[ gcWorker.work ];
  DaoTypeBase *typer;
  const short work = gcWorker.work;
  const short idle = gcWorker.idle;
  int boundary = gcWorker.boundary;
  int i = gcWorker.ii;
  int j = 0;

  for( ; i<boundary; i++ ){
    DaoBase *dbase = pool->items.pBase[i];
    dbase->gcState[work] = 0;
    j ++;
    if( dbase->cycRefCount==0 ){
      if( dbase->refCount !=0 ){
        printf(" refCount not zero %p %i: %i, %i\n", dbase, dbase->type, dbase->refCount, dbase->subType );
        if( ! ( dbase->gcState[ idle ] & GC_IN_POOL ) ){
          dbase->gcState[ idle ] = GC_IN_POOL;
          DArray_Append( gcWorker.pool[idle], dbase );
        }
        continue;
      }
      if( ! ( dbase->gcState[idle] & GC_IN_POOL ) ){
#ifdef DAO_GC_PROF
        ObjectProfile[dbase->type] --;
#endif
        /*
        if( dbase->type <= DAO_VMPROCESS )
        if( dbase->type == DAO_STRING ){
          DaoString *s = (DaoString*) dbase;
          if( s->chars->mbs && s->chars->mbs->refCount > 1 ){
            printf( "delete mbstring!!! %i\n", s->chars->mbs->refCount );
          }
          if( s->chars->wcs && s->chars->wcs->refCount > 1 ){
            printf( "delete wcstring!!! %i\n", s->chars->wcs->refCount );
          }
        }
        if( dbase->type < DAO_STRING )
        */
        typer = DaoBase_GetTyper( dbase );
        typer->Delete( dbase );
      }
    }
    if( j >= gcWorker.gcMin ) break;
  }
#ifdef DAO_GC_PROF
  printf("heap[idle] = %i;\theap[work] = %i\n", gcWorker.pool[ idle ]->size, gcWorker.pool[ work ]->size );
  printf("=======================================\n");
  printf( "mbs count = %i\n", daoCountMBS );
  printf( "array count = %i\n", daoCountArray );
  for(k=0; k<100; k++){
    if( ObjectProfile[k] > 0 ){
      printf( "type = %3i; rest = %5i\n", k, ObjectProfile[k] );
    }
  }
#endif
  if( i >= boundary ){
    gcWorker.ii = 0;
    gcWorker.workType = 0;
    gcWorker.count = 0;
    DArray_Clear( pool );
  }else{
    gcWorker.ii = i+1;
  }
}
void cycRefCountDecrement( DaoBase *dbase )
{
  const short work = gcWorker.work;
  if( !dbase ) return;
  if( ! ( dbase->gcState[work] & GC_IN_POOL ) ){
    DArray_Append( gcWorker.pool[work], dbase );
    dbase->gcState[work] = GC_IN_POOL;
    dbase->cycRefCount = dbase->refCount;
  }
  dbase->cycRefCount --;

  if( dbase->cycRefCount<0 ){
    /*
       printf("cycRefCount<0 : %i\n", dbase->type);
     */
    dbase->cycRefCount = 0;
  }
}
void cycRefCountIncrement( DaoBase *dbase )
{
  const short work = gcWorker.work;
  if( dbase ){
    dbase->cycRefCount++;
    if( ! ( dbase->gcState[work] & GC_MARKED ) ){
      DArray_Append( gcWorker.pool[work], dbase );
    }
  }
}
#endif
void cycRefCountDecrements( DArray *list )
{
  DaoBase **dbases;
  int i;
  if( list == NULL ) return;
  dbases = list->items.pBase;
  for( i=0; i<list->size; i++ ) cycRefCountDecrement( dbases[i] );
}
void cycRefCountIncrements( DArray *list )
{
  DaoBase **dbases;
  int i;
  if( list == NULL ) return;
  dbases = list->items.pBase;
  for( i=0; i<list->size; i++ ) cycRefCountIncrement( dbases[i] );
}
void directRefCountDecrement( DArray *list )
{
  DaoBase **dbases;
  int i;
  if( list == NULL ) return;
  dbases = list->items.pBase;
  for( i=0; i<list->size; i++ ) if( dbases[i] ) dbases[i]->refCount --;
  DArray_Clear( list );
}
void cycRefCountDecrementV( DValue value )
{
  if( value.t < DAO_ARRAY ) return;
  cycRefCountDecrement( value.v.p );
}
void cycRefCountIncrementV( DValue value )
{
  if( value.t < DAO_ARRAY ) return;
  cycRefCountIncrement( value.v.p );
}
void cycRefCountDecrementsV( DVarray *list )
{
  int i;
  DValue *data;
  if( list == NULL ) return;
  data = list->data;
  for( i=0; i<list->size; i++ ) cycRefCountDecrementV( data[i] );
}
void cycRefCountIncrementsV( DVarray *list )
{
  int i;
  DValue *data;
  if( list == NULL ) return;
  data = list->data;
  for( i=0; i<list->size; i++ ) cycRefCountIncrementV( data[i] );
}
void directRefCountDecrementV( DVarray *list )
{
  int i;
  DValue *data;
  if( list == NULL ) return;
  data = list->data;
  for( i=0; i<list->size; i++ ){
    if( data[i].t >= DAO_ARRAY ){
      data[i].v.p->refCount --;
      data[i].t = 0;
    }
  }
  DVarray_Clear( list );
}
void cycRefCountDecrementsT( DPtrTuple *list )
{
  int i;
  DaoBase **dbases;
  if( list ==NULL ) return;
  dbases = list->items.pBase;
  for( i=0; i<list->size; i++ ) cycRefCountDecrement( dbases[i] );
}
void cycRefCountIncrementsT( DPtrTuple *list )
{
  int i;
  DaoBase **dbases;
  if( list ==NULL ) return;
  dbases = list->items.pBase;
  for( i=0; i<list->size; i++ ) cycRefCountIncrement( dbases[i] );
}
void directRefCountDecrementT( DPtrTuple *list )
{
  int i;
  DaoBase **dbases;
  if( list ==NULL ) return;
  dbases = list->items.pBase;
  for( i=0; i<list->size; i++ ) if( dbases[i] ) dbases[i]->refCount --;
  DPtrTuple_Clear( list );
}
void cycRefCountDecrementsVT( DVaTuple *list )
{
  int i;
  DValue *data;
  if( list ==NULL ) return;
  data = list->data;
  for( i=0; i<list->size; i++ ) cycRefCountDecrementV( data[i] );
}
void cycRefCountIncrementsVT( DVaTuple *list )
{
  int i;
  DValue *data;
  if( list ==NULL ) return;
  data = list->data;
  for( i=0; i<list->size; i++ ) cycRefCountIncrementV( data[i] );
}
void directRefCountDecrementVT( DVaTuple *list )
{
  int i;
  DValue *data;
  if( list ==NULL ) return;
  data = list->data;
  for( i=0; i<list->size; i++ ){
    if( data[i].t >= DAO_ARRAY ){
      data[i].v.p->refCount --;
      data[i].t = 0;
    }
  }
  DVaTuple_Clear( list );
}
