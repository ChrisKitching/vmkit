//===-------- JavaClass.cpp - Java class representation -------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <vector>

#include <string.h>

#include "mvm/JIT.h"
#include "types.h"

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaConstantPool.h"
#include "JavaJIT.h"
#include "JavaObject.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "Jnjvm.h"
#include "JnjvmModuleProvider.h"
#include "Reader.h"

using namespace jnjvm;

const int CommonClass::MaxDisplay = 6;

const UTF8* Attribut::codeAttribut = 0;
const UTF8* Attribut::exceptionsAttribut = 0;
const UTF8* Attribut::constantAttribut = 0;
const UTF8* Attribut::lineNumberTableAttribut = 0;
const UTF8* Attribut::innerClassesAttribut = 0;
const UTF8* Attribut::sourceFileAttribut = 0;

JavaObject* CommonClass::jnjvmClassLoader = 0;

CommonClass* ClassArray::SuperArray = 0;
std::vector<Class*> ClassArray::InterfacesArray;
std::vector<JavaMethod*> ClassArray::VirtualMethodsArray;
std::vector<JavaMethod*> ClassArray::StaticMethodsArray;
std::vector<JavaField*> ClassArray::VirtualFieldsArray;
std::vector<JavaField*> ClassArray::StaticFieldsArray;

void Attribut::derive(const UTF8* name, unsigned int length,
                      const Reader* reader) {
  
  this->start    = reader->cursor;
  this->nbb      = length;
  this->name     = name;

}

Attribut* Class::lookupAttribut(const UTF8* key ) {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e; ++i) {
    Attribut* cur = *i;
    if (cur->name->equals(key)) return cur;
  }

  return 0;
}

Attribut* JavaField::lookupAttribut(const UTF8* key ) {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e;++i) {
    Attribut* cur = *i;
    if (cur->name->equals(key)) return cur;
  }

  return 0;
}

Attribut* JavaMethod::lookupAttribut(const UTF8* key ) {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e; ++i) {
    Attribut* cur = *i;
    if (cur->name->equals(key)) return cur;
  }

  return 0;
}

void Class::destroyer(size_t sz) {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e;) {
    Attribut* cur = *i;
    delete cur;
  }

}

Reader* Attribut::toReader(Jnjvm* vm, ArrayUInt8* array, Attribut* attr) {
  return vm_new(vm, Reader)(array, attr->start, attr->nbb);
}


static void printClassNameIntern(const UTF8* name, unsigned int start,
                                 unsigned int end,  mvm::PrintBuffer* buf) {
  
  uint16 first = name->elements[start];
  if (first == AssessorDesc::I_TAB) {
    unsigned int stepsEnd = start;
    while (name->elements[stepsEnd] == AssessorDesc::I_TAB) stepsEnd++;
    if (name->elements[stepsEnd] == AssessorDesc::I_REF) {
      printClassNameIntern(name, (stepsEnd + 1),(end - 1), buf);
    } else {
      AssessorDesc * funcs = 0;
      uint32 next = 0;
      AssessorDesc::analyseIntern(name, stepsEnd, 0, funcs, next);
      buf->write(funcs->asciizName);
    }
    buf->write(" ");
    for (uint32 i = start; i < stepsEnd; i++)
      buf->write("[]");
  } else {
    char* tmp = (char*)(alloca(1 + (end - start)));
    for (uint32 i = start; i < end; i++) {
      short int cur = name->elements[i];
      tmp[i - start] = (cur == '/' ? '.' : cur);
    }
    tmp[end - start] = 0;
    buf->write(tmp);
  }
}

void CommonClass::printClassName(const UTF8* name, mvm::PrintBuffer* buf) {
  printClassNameIntern(name, 0, name->size, buf);
}

void CommonClass::print(mvm::PrintBuffer* buf) const {
  buf->write("CommonClass<");
  printClassName(name, buf);
  buf->write(">");
}

void CommonClass::initialise(Jnjvm* isolate, bool isArray) {
  this->lockVar = mvm::Lock::allocRecursive();
  this->condVar = mvm::Cond::allocCond();
  this->status = hashed;
  this->isolate = isolate;
  this->isArray = isArray;
#ifndef MULTIPLE_VM
  this->delegatee = 0;
#endif
}

void Class::print(mvm::PrintBuffer* buf) const {
  buf->write("Class<");
  printClassName(name, buf);
  buf->write(">");
}

void ClassArray::print(mvm::PrintBuffer* buf) const {
  buf->write("ClassArray<");
  printClassName(name, buf);
  buf->write(">");
}

void ClassArray::resolveComponent() {
  AssessorDesc::introspectArray(isolate, classLoader, name, 0, _funcs,
                                _baseClass);
}

JavaObject* ClassArray::arrayLoader(Jnjvm* isolate, const UTF8* name,
                                    JavaObject* loader,
                                    unsigned int start, unsigned int len) {
  
  if (name->elements[start] == AssessorDesc::I_TAB) {
    return arrayLoader(isolate, name, loader, start + 1, len - 1);
  } else if (name->elements[start] == AssessorDesc::I_REF) {
    const UTF8* componentName = name->javaToInternal(isolate, start + 1,
                                                     len - 2);
    CommonClass* cl = isolate->loadName(componentName, loader, false, false,
                                        true);
    return cl->classLoader;
  } else {
    return 0;
  }
}

void* JavaMethod::_compiledPtr() {
  if (code != 0) return code;
  else {
    classDef->aquire();
    if (code == 0) {
      void* val = 
        classDef->isolate->TheModuleProvider->materializeFunction(this);
#ifndef MULTIPLE_GC
      mvm::Code* temp = (mvm::Code*)(Collector::begOf(val));
#else
      mvm::Code* temp = (mvm::Code*)(classDef->isolate->GC->begOf(val));
#endif
      if (temp) {
        temp->method()->definition(this);
      }
      code = (mvm::Code*)val;
      classDef->release();
    } else {
      classDef->release();
    }
    return code;
  }
}

void JavaMethod::print(mvm::PrintBuffer* buf) const {
  buf->write("JavaMethod<");
  signature->printWithSign(classDef, name, buf);
  buf->write(">");
}

void JavaField::print(mvm::PrintBuffer* buf) const {
  buf->write("JavaField<");
  if (isStatic(access))
    buf->write("static ");
  else
    buf->write("virtual ");
  signature->tPrintBuf(buf);
  buf->write(" ");
  classDef->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
}

JavaMethod* CommonClass::lookupMethodDontThrow(const UTF8* name,
                                               const UTF8* type, bool isStatic,
                                               bool recurse) {
  
  std::vector<JavaMethod*>* meths = (isStatic? &staticMethods : 
                                               &virtualMethods);
  
  JavaMethod *cur = 0;
  
  for (std::vector<JavaMethod*>::iterator i = meths->begin(),
       e = meths->end(); i!= e; i++) {
    cur = *i;
    if (cur->name->equals(name) && cur->type->equals(type)) {
      return cur;
    }
  }
  cur = 0;

  if (recurse) {
    if (super) cur = super->lookupMethodDontThrow(name, type, isStatic,
                                                  recurse);
    if (cur) return cur;
    if (isStatic) {
      for (std::vector<Class*>::iterator i = interfaces.begin(),
           e = interfaces.end(); i!= e; i++) {
        cur = (*i)->lookupMethodDontThrow(name, type, isStatic, recurse);
        if (cur) return cur;
      }
    }
  }

  return 0;
}

JavaMethod* CommonClass::lookupMethod(const UTF8* name, const UTF8* type,
                                      bool isStatic, bool recurse) {
  JavaMethod* res = lookupMethodDontThrow(name, type, isStatic, recurse);
  if (!res) {
    JavaThread::get()->isolate->error(Jnjvm::NoSuchMethodError, 
                                      "unable to find %s in %s",
                                      name->printString(), this->printString());
  }
  return res;
}

JavaField* CommonClass::lookupFieldDontThrow(const UTF8* name,
                                             const UTF8* type, bool isStatic,
                                             bool recurse) {

  std::vector<JavaField*>* fields = (isStatic? &staticFields : &virtualFields);
  
  JavaField *cur = 0;

  for (std::vector<JavaField*>::iterator i = fields->begin(),
       e = fields->end(); i!= e; i++) {
    cur = *i;
    if (cur->name->equals(name) && cur->type->equals(type)) {
      return cur;
    }
  }

  cur = 0;

  if (recurse) {
    if (super) cur = super->lookupFieldDontThrow(name, type, isStatic,
                                                 recurse);
    if (cur) return cur;
    if (isStatic) {
      for (std::vector<Class*>::iterator i = interfaces.begin(),
           e = interfaces.end(); i!= e; i++) {
        cur = (*i)->lookupFieldDontThrow(name, type, isStatic, recurse);
        if (cur) return cur;
      }
    }
  }

  return 0;
}

JavaField* CommonClass::lookupField(const UTF8* name, const UTF8* type,
                                    bool isStatic, bool recurse) {
  
  JavaField* res = lookupFieldDontThrow(name, type, isStatic, recurse);
  if (!res) {
    JavaThread::get()->isolate->error(Jnjvm::NoSuchFieldError, 
                                      "unable to find %s in %s",
                                      name->printString(), this->printString());
  }
  return res;
}

JavaObject* Class::doNew(Jnjvm* vm) {
  JavaObject* res = (JavaObject*)vm->allocateObject(virtualSize, virtualVT);
  res->classOf = this;
  return res;
}

bool CommonClass::inheritName(const UTF8* Tname) {
  if (name->equals(Tname)) {
    return true;
  } else  if (AssessorDesc::bogusClassToPrimitive(this)) {
    return true;
  } else if (super) {
    if (super->inheritName(Tname)) return true;
  }
  
  for (uint32 i = 0; i < interfaces.size(); ++i) {
    if (interfaces[i]->inheritName(Tname)) return true;
  }
  return false;
}

bool CommonClass::isOfTypeName(const UTF8* Tname) {
  if (inheritName(Tname)) {
    return true;
  } else if (isArray) {
    CommonClass* curS = this;
    uint32 prof = 0;
    uint32 len = Tname->size;
    bool res = true;
    
    while (res && Tname->elements[prof] == AssessorDesc::I_TAB) {
      CommonClass* cl = ((ClassArray*)curS)->baseClass();
      Jnjvm *vm = cl->isolate;
      ++prof;
      vm->resolveClass(cl, false);
      res = curS->isArray && cl && (prof < len);
      curS = cl;
    }
    
    Jnjvm *vm = this->isolate;
    return (Tname->elements[prof] == AssessorDesc::I_REF) &&  
      (res && curS->inheritName(Tname->extract(vm, prof + 1, len - 1)));
  } else {
    return false;
  }
}

bool CommonClass::implements(CommonClass* cl) {
  if (this == cl) return true;
  else {
    for (std::vector<Class*>::iterator i = interfaces.begin(),
         e = interfaces.end(); i!= e; i++) {
      if (*i == cl) return true;
      else if ((*i)->implements(cl)) return true;
    }
    if (super) {
      return super->implements(cl);
    }
  }
  return false;
}

bool CommonClass::instantiationOfArray(CommonClass* cl) {
  if (this == cl) return true;
  else {
    if (isArray && cl->isArray) {
      CommonClass* baseThis = ((ClassArray*)this)->baseClass();
      CommonClass* baseCl = ((ClassArray*)cl)->baseClass();

      if (isInterface(baseThis->access) && isInterface(baseCl->access)) {
        return baseThis->implements(baseCl);
      } else {
        return baseThis->isAssignableFrom(baseCl);
      }
    }
  }
  return false;
}

bool CommonClass::subclassOf(CommonClass* cl) {
  if (cl->depth < display.size()) {
    return display[cl->depth] == cl;
  } else {
    return false;
  }
}

bool CommonClass::isAssignableFrom(CommonClass* cl) {
  if (this == cl) {
    return true;
  } else if (isInterface(cl->access)) {
    return this->implements(cl);
  } else if (cl->isArray) {
    return this->instantiationOfArray(cl);
  } else {
    return this->subclassOf(cl);
  }
}

void JavaField::initField(JavaObject* obj) {
  const AssessorDesc* funcs = signature->funcs;
  Attribut* attribut = lookupAttribut(Attribut::constantAttribut);

  if (!attribut) {
    JnjvmModule::InitField(this, obj);
  } else {
    Reader* reader = attribut->toReader(classDef->isolate,
                                        classDef->bytes, attribut);
    JavaCtpInfo * ctpInfo = classDef->ctpInfo;
    uint16 idx = reader->readU2();
    if (funcs == AssessorDesc::dLong) {
      JnjvmModule::InitField(this, obj, (uint64)ctpInfo->LongAt(idx));
    } else if (funcs == AssessorDesc::dDouble) {
      JnjvmModule::InitField(this, obj, ctpInfo->DoubleAt(idx));
    } else if (funcs == AssessorDesc::dFloat) {
      JnjvmModule::InitField(this, obj, ctpInfo->FloatAt(idx));
    } else if (funcs == AssessorDesc::dRef) {
      const UTF8* utf8 = ctpInfo->UTF8At(ctpInfo->ctpDef[idx]);
      JnjvmModule::InitField(this, obj,
                         (JavaObject*)ctpInfo->resolveString(utf8, idx));
    } else if (funcs == AssessorDesc::dInt || funcs == AssessorDesc::dChar ||
               funcs == AssessorDesc::dShort || funcs == AssessorDesc::dByte ||
               funcs == AssessorDesc::dBool) {
      JnjvmModule::InitField(this, obj, (uint64)ctpInfo->IntegerAt(idx));
    } else {
      JavaThread::get()->isolate->
        unknownError("unknown constant %c", funcs->byteId);
    }
  }
  
}

JavaObject* CommonClass::getClassDelegatee() {
  return JavaThread::get()->isolate->getClassDelegatee(this);
}

void CommonClass::resolveClass(bool doClinit) {
  isolate->resolveClass(this, doClinit);
}

void CommonClass::initialiseClass() {
  return isolate->initialiseClass(this);
}

#ifdef MULTIPLE_VM
JavaObject* Class::staticInstance() {
  std::pair<JavaState, JavaObject*>* val = 
    JavaThread::get()->isolate->statics->lookup(this);
  assert(val);
  return val->second;
}

void Class::createStaticInstance() {
  JavaObject* val = 
    (JavaObject*)JavaThread::get()->isolate->allocateObject(staticSize,
                                                            staticVT);
  val->initialise(this);
  for (std::vector<JavaField*>::iterator i = this->staticFields.begin(),
            e = this->staticFields.end(); i!= e; ++i) {
    
    (*i)->initField(val);
  }
  
  Jnjvm* vm = JavaThread::get()->isolate;
  std::pair<JavaState, JavaObject*>* p = vm->statics->lookup(this);
  assert(p);
  assert(!p->second);
  p->second = val;
}

JavaState* CommonClass::getStatus() {
  if (!this->isArray && 
      !AssessorDesc::bogusClassToPrimitive(this)) {
    Class* cl = (Class*)this;
    Jnjvm* vm = JavaThread::get()->isolate;
    std::pair<JavaState, JavaObject*>* val = vm->statics->lookup(cl);
    if (!val) {
      val = new std::pair<JavaState, JavaObject*>(status, 0);
      JavaThread::get()->isolate->statics->hash(cl, val);
    }
    if (val->first < status) val->first = status;
    return (JavaState*)&(val->first);
  } else {
    return &status;
  }
}
#endif
