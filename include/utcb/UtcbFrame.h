/*
 * TODO comment me
 *
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NUL.
 *
 * NUL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NUL is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

#include <arch/UtcbExcLayout.h>
#include <utcb/Utcb.h>
#include <cap/CapRange.h>
#include <Exception.h>
#include <kobj/Ec.h>
#include <String.h>
#include <cstring>
#include <Assert.h>
#include <Math.h>

namespace nul {

class Pt;
class UtcbFrameRef;
OStream &operator<<(OStream &os,const Utcb &utcb);
OStream &operator<<(OStream &os,const UtcbFrameRef &frm);

class UtcbException : public Exception {
public:
	explicit UtcbException(ErrorCode code) throw() : Exception(code) {
	}
};

class UtcbFrameRef {
	friend class Pt;
	friend class Utcb;
	friend OStream &operator<<(OStream &os,const Utcb &utcb);
	friend 	OStream &operator<<(OStream &os,const UtcbFrameRef &frm);

public:
	enum DelFlags {
		NONE	= 0,
		FROM_HV	= 0x800,		// source = hypervisor
		UPD_GPT	= 0x400,		// update guest page table
		UPD_DPT	= 0x200,		// update DMA page table
	};

private:
	class TypedItem {
		friend class UtcbFrameRef;
	public:
		enum {
			TYPE_XLT		= 0,
			TYPE_DEL		= 1,
		};

		explicit TypedItem(Crd crd = Crd(0),word_t aux = 0) : _crd(crd), _aux(aux) {
		}

		Crd crd() const {
			return _crd;
		}
		word_t aux() const {
			return _aux;
		}

	private:
		Crd _crd;
		word_t _aux;
	};

	class XltItem : public TypedItem {
	public:
		explicit XltItem(Crd crd) : TypedItem(crd,TYPE_XLT) {
		}
	};

	class DelItem : public TypedItem {
	public:
		explicit DelItem(Crd crd,unsigned flags,word_t hotspot = 0)
			: TypedItem(crd,TYPE_DEL | flags | (hotspot << 12)) {
		}
	};

	void check_untyped_write(size_t u) {
		if(free_untyped() < u)
			throw UtcbException(E_CAPACITY);
	}
	void check_typed_write(size_t t) {
		if(free_typed() < t)
			throw UtcbException(E_CAPACITY);
	}
	void check_untyped_read(size_t u) {
		if(_upos + u > untyped())
			throw UtcbException(E_UTCB_UNTYPED);
	}
	void check_typed_read(size_t t) {
		if(_tpos + t > typed())
			throw UtcbException(E_UTCB_TYPED);
	}

	explicit UtcbFrameRef(Utcb *utcb,size_t top)
			: _utcb(utcb), _top(Utcb::get_top(utcb,top)), _upos(), _tpos() {
	}
public:
	explicit UtcbFrameRef(Utcb *utcb = Ec::current()->utcb())
			: _utcb(utcb), _top(Utcb::get_top(_utcb)), _upos(), _tpos() {
		_utcb = Utcb::get_current_frame(_utcb);
	}
	virtual ~UtcbFrameRef() {
	}

	void clear() {
		_utcb->mtr = 0;
	}
	void reset() {
		_utcb->reset();
		_upos = _tpos = 0;
	}

	// TODO for mem and io as well?
	void accept_translates(word_t base = 0,uint order = 31) {
		set_translate_crd(Crd(base,order,Crd::OBJ_ALL));
	}
	void accept_delegates() {
		accept_delegates(get_receive_crd().order());
	}
	void accept_delegates(uint order) {
		capsel_t caps = CapSpace::get().allocate(1 << order,1 << order);
		set_receive_crd(Crd(caps,order,Crd::OBJ_ALL));
	}
	void finish_input() {
		if(has_more_untyped() || has_more_typed())
			throw UtcbException(E_ARGS_INVALID);
		clear();
	}

	size_t free_untyped() const {
		return _utcb->free_untyped();
	}
	size_t free_typed() const {
		return _utcb->free_typed();
	}
	size_t untyped() const {
		return _utcb->untyped;
	}
	size_t typed() const {
		return _utcb->typed;
	}
	bool has_more_typed() const {
		return _tpos < typed();
	}
	bool has_more_untyped() const {
		return _upos < untyped();
	}

	Crd get_receive_crd() const {
		return Crd(_utcb->crd);
	}
	void set_receive_crd(Crd crd) {
		_utcb->crd = crd.value();
	}
	Crd get_translate_crd() const {
		return Crd(_utcb->crd_translate);
	}
	void set_translate_crd(Crd crd) {
		_utcb->crd_translate = crd.value();
	}

	// TODO for mem and io as well?
	void translate(capsel_t cap,uint perms = Crd::OBJ_ALL) {
		add_typed(XltItem(Crd(cap,0,Crd::OBJ | perms)));
	}
	void translate(const CapRange& range) {
		size_t count = range.count();
		uintptr_t start = range.start();
		while(count > 0) {
			uint minshift = Math::minshift(start,count);
			add_typed(XltItem(Crd(start,minshift,range.attr())));
			start += 1 << minshift;
			count -= 1 << minshift;
		}
	}
	void delegate(capsel_t cap,uint hotspot = 0,DelFlags flags = NONE,uint perms = Crd::OBJ_ALL) {
		add_typed(DelItem(Crd(cap,0,Crd::OBJ | perms),flags,hotspot));
	}
	void delegate(const CapRange& range,DelFlags flags = NONE) {
		uintptr_t hotspot = range.hotspot() ? range.hotspot() : range.start();
		size_t count = range.count();
		uintptr_t start = range.start();
		while(count > 0) {
			uint minshift = Math::minshift(start | hotspot,count);
			add_typed(DelItem(Crd(start,minshift,range.attr()),flags,hotspot));
			start += 1 << minshift;
			hotspot += 1 << minshift;
			count -= 1 << minshift;
		}
	}
	void add_typed(const TypedItem &item) {
		// ensure that we're the current frame
		assert(Utcb::get_current_frame(_utcb->base()) == _utcb);
		check_typed_write(1);
		_top[-(_utcb->typed * 2 + 1)] = item._aux;
		_top[-(_utcb->typed * 2 + 2)] = item._crd.value();
		_utcb->typed++;
	}

	Crd get_translated(uint order = 0) {
		TypedItem ti;
		get_typed(ti);
		if(ti.crd().offset() == 0 || (ti.aux() & TypedItem::TYPE_DEL) || ti.crd().order() != order)
			throw UtcbException(E_ARGS_INVALID);
		return ti.crd();
	}
	Crd get_delegated(uint order = 0) {
		TypedItem ti;
		get_typed(ti);
		if(ti.crd().offset() == 0 || !(ti.aux() & TypedItem::TYPE_DEL) || ti.crd().order() != order)
			throw UtcbException(E_ARGS_INVALID);
		return ti.crd();
	}
	void get_typed(TypedItem& item) {
		check_typed_read(1);
		item._aux = _top[-(_tpos * 2 + 1)];
		item._crd = Crd(_top[-(_tpos * 2 + 2)]);
		_tpos++;
	}

	template<typename T>
	UtcbFrameRef &operator <<(const T& value) {
		const size_t words = (sizeof(T) + sizeof(word_t) - 1) / sizeof(word_t);
		check_untyped_write(words);
		assert(Utcb::get_current_frame(_utcb->base()) == _utcb);
		*reinterpret_cast<T*>(_utcb->msg + untyped()) = value;
		_utcb->untyped += words;
		return *this;
	}
	UtcbFrameRef &operator <<(const String& value) {
		const size_t words = Math::blockcount(value.length(),sizeof(word_t)) + 1;
		check_untyped_write(words);
		assert(Utcb::get_current_frame(_utcb->base()) == _utcb);
		*reinterpret_cast<size_t*>(_utcb->msg + untyped()) = value.length();
		memcpy(_utcb->msg + untyped() + 1,value.str(),value.length());
		_utcb->untyped += words;
		return *this;
	}

	template<typename T>
	UtcbFrameRef &operator >>(T &value) {
		const size_t words = (sizeof(T) + sizeof(word_t) - 1) / sizeof(word_t);
		check_untyped_read(words);
		value = *reinterpret_cast<T*>(_utcb->msg + _upos);
		_upos += words;
		return *this;
	}
	UtcbFrameRef &operator >>(String &value) {
		check_untyped_read(1);
		size_t len = *reinterpret_cast<size_t*>(_utcb->msg + _upos);
		const size_t words = Math::blockcount(len,sizeof(word_t)) + 1;
		check_untyped_read(words);
		value.reset(reinterpret_cast<const char*>(_utcb->msg + _upos + 1),len);
		_upos += words;
		return *this;
	}

private:
	UtcbFrameRef(const UtcbFrameRef&);
	UtcbFrameRef& operator=(const UtcbFrameRef&);

protected:
	Utcb *_utcb;
	word_t *_top;
	size_t _upos;
	size_t _tpos;
};

class UtcbFrame : public UtcbFrameRef {
public:
	explicit UtcbFrame() : UtcbFrameRef() {
		_utcb = _utcb->push(_top);
	}
	virtual ~UtcbFrame() {
		_utcb->pop();
	}
};

class UtcbExcFrameRef : public UtcbFrameRef {
public:
	explicit UtcbExcFrameRef() : UtcbFrameRef() {
	}
	~UtcbExcFrameRef() {
	}

	UtcbExc *operator->() {
		return reinterpret_cast<UtcbExc*>(_utcb);
	}
};

}
