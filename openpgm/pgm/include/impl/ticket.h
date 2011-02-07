/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * Ticket spinlocks per Jonathan Corbet on LKML and Leslie Lamport's
 * Bakery algorithm.  Read-write version per David Howell on LKML derived
 * from Joseph Seigh at IBM.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_TICKET_H__
#define __PGM_IMPL_TICKET_H__

typedef union pgm_ticket_t pgm_ticket_t;
typedef union pgm_rwticket_t pgm_rwticket_t;

#ifndef _WIN32
#	include <pthread.h>
#	include <unistd.h>
#else
#	define VC_EXTRALEAN
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif
#include <pgm/types.h>
#include <pgm/atomic.h>
#include <impl/thread.h>

PGM_BEGIN_DECLS

/* Byte alignment for packet memory maps.
 * NB: Solaris and OpenSolaris don't support #pragma pack(push) even on x86.
 */
#if defined( __GNUC__ ) && !defined( __sun )
#	pragma pack(push)
#endif
#pragma pack(1)

union pgm_ticket_t {
#if defined( _WIN32 )
	volatile LONG		pgm_tkt_data32;
	struct {
		volatile SHORT		pgm_un_ticket;
		volatile SHORT		pgm_un_user;
	} pgm_un;
#else
	volatile uint32_t	pgm_tkt_data32;
	struct {
		volatile uint16_t	pgm_un_ticket;
		volatile uint16_t	pgm_un_user;
	} pgm_un;
#endif
};

#define pgm_tkt_ticket	pgm_un.pgm_un_ticket
#define pgm_tkt_user	pgm_un.pgm_un_user


union pgm_rwticket_t {
#if defined( _WIN32 )
	volatile LONG		pgm_rwtkt_data32;
	struct {
		union {
			volatile SHORT		pgm_un2_data16;
			struct {
				volatile CHAR		pgm_un3_write;
				volatile CHAR		pgm_un3_read;
			} pgm_un3;
		} pgm_un2;
		volatile CHAR		pgm_un_user;
	} pgm_un;
#else
	volatile uint32_t	pgm_rwtkt_data32;
	struct {
		union {
			volatile uint16_t	pgm_un2_data16;
			struct {
				volatile uint8_t	pgm_un3_write;
				volatile uint8_t	pgm_un3_read;
			} pgm_un3;
		} pgm_un2;
		volatile uint8_t	pgm_un_user;
	} pgm_un;
#endif
};

#define pgm_rwtkt_data16	pgm_un.pgm_un2.pgm_un2_data16
#define pgm_rwtkt_write		pgm_un.pgm_un2.pgm_un3.pgm_un3_write
#define pgm_rwtkt_read		pgm_un.pgm_un2.pgm_un3.pgm_un3_read
#define pgm_rwtkt_user		pgm_un.pgm_un_user

#if defined( __GNUC__ ) && !defined( __sun )
#	pragma pack(pop)
#else
#	pragma pack()
#endif

/* additional required atomic ops */

/* 32-bit word CAS, returns TRUE if swap occurred.
 *
 *	if (*atomic == oldval) {
 *		*atomic = newval;
 *		return TRUE;
 *	}
 *	return FALSE;
 */

static inline
bool
pgm_atomic_compare_and_exchange32 (
	volatile uint32_t*	atomic,
	const uint32_t		newval,
	const uint32_t		oldval
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	uint8_t result;
	__asm__ volatile ("lock; cmpxchgl %2, %0\n\t"
			  "setz %1\n\t"
			: "+m" (*atomic), "=r" (result)
			: "ir" (newval),  "a" (oldval)
			: "memory", "cc"  );
	return (bool)result;
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
	return __sync_bool_compare_and_swap (atomic, oldval, newval);
#elif defined( _WIN32 )
	return (oldval == _InterlockedCompareExchange ((volatile LONG*)atomic, newval, oldval));
#endif
}

/* byte addition.
 *
 *	*atomic += val;
 */

static inline
void
pgm_atomic_add8 (
	volatile uint8_t*	atomic,
	const uint8_t		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; addb %1, %0"
			: "=m" (*atomic)
			: "ir" (val), "m" (*atomic)
			: "memory", "cc"  );
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
/* interchangable with __sync_fetch_and_add () */
	__sync_add_and_fetch (atomic, val);
#elif defined( _WIN32 )
/* there is no _InterlockedExchangeAdd8() */
#endif
}

/* byte addition returning original atomic value.
 *
 *	uint8_t oldval = *atomic;
 *	*atomic += val;
 *	return oldval;
 */

static inline
uint8_t
pgm_atomic_fetch_and_add8 (
	volatile uint8_t*	atomic,
	const uint8_t		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	uint8_t result;
	__asm__ volatile ("lock; xaddb %0, %1"
			: "=r" (result), "=m" (*atomic)
			: "0" (val), "m" (*atomic)
			: "memory", "cc"  );
	return result;
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
	return __sync_fetch_and_add (atomic, val);
#elif defined( _WIN32 )
/* there is no _InterlockedExchangeAdd8() */
#endif
}

/* byte increment.
 *
 *	*atomic++;
 */

static inline
void
pgm_atomic_inc8 (
	volatile uint8_t*	atomic
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; incb %0"
			: "+m" (*atomic)
			:
			: "memory", "cc"  );
#else
/* there is no _InterlockedIncrement8() */
	pgm_atomic_add8 (atomic, 1);
#endif
}

/* byte increment returning original atomic value.
 *
 *	uint8_t tmp = *atomic++;
 *	return tmp;
 */

static inline
uint8_t
pgm_atomic_fetch_and_inc8 (
	volatile uint8_t*	atomic
	)
{
/* there is no _InterlockedIncrement8() */
	return pgm_atomic_fetch_and_add8 (atomic, 1);
}

/* 16-bit word addition.
 */

static inline
void
pgm_atomic_add16 (
	volatile uint16_t*	atomic,
	const uint16_t		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; addw %1, %0"
			: "=m" (*atomic)
			: "ir" (val), "m" (*atomic)
			: "memory", "cc"  );
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
/* interchangable with __sync_fetch_and_add () */
	__sync_add_and_fetch (atomic, val);
#elif defined( _WIN32 )
/* there is no _InterlockedExchangeAdd16() */
#endif
}

/* 16-bit word addition returning original atomic value.
 */

static inline
uint16_t
pgm_atomic_fetch_and_add16 (
	volatile uint16_t*	atomic,
	const uint16_t		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	uint16_t result;
	__asm__ volatile ("lock; xaddw %0, %1"
			: "=r" (result), "=m" (*atomic)
			: "0" (val), "m" (*atomic)
			: "memory", "cc"  );
	return result;
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
	return __sync_fetch_and_add (atomic, val);
#elif defined( _WIN32 )
/* there is no _InterlockedExchangeAdd16() */
#endif
}

/* 16-bit word increment.
 */

static inline
void
pgm_atomic_inc16 (
	volatile uint16_t*	atomic
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; incw %0"
			: "+m" (*atomic)
			:
			: "memory", "cc"  );
#elif defined( _WIN32 )
	_InterlockedIncrement16 ((volatile SHORT*)atomic);
#else
	pgm_atomic_add16 (atomic, 1);
#endif
}

/* 16-bit word increment returning original atomic value.
 */

static inline
uint16_t
pgm_atomic_fetch_and_inc16 (
	volatile uint16_t*	atomic
	)
{
#if defined( _WIN32 )
	return _InterlockedIncrement16 ((volatile SHORT*)atomic);
#else
	return pgm_atomic_fetch_and_add16 (atomic, 1);
#endif
}

/* 16-bit word load.
 */

static inline
uint16_t
pgm_atomic_read16 (
	const volatile uint16_t* atomic
	)
{
	return *atomic;
}

/* 16-bit word store.
 */

static inline
void
pgm_atomic_write16 (
	volatile uint16_t*	atomic,
	const uint16_t		val
	)
{
	*atomic = val;
}


/* ticket spinlocks */

static inline void pgm_ticket_init (pgm_ticket_t* ticket) {
	ticket->pgm_tkt_data32 = 0;
}

static inline void pgm_ticket_free (pgm_ticket_t* ticket) {
/* nop */
	(void)ticket;
}

static inline bool pgm_ticket_trylock (pgm_ticket_t* ticket) {
	const uint16_t user = ticket->pgm_tkt_user;
	pgm_ticket_t exchange, comparand;
	comparand.pgm_tkt_user = comparand.pgm_tkt_ticket = exchange.pgm_tkt_ticket = user;
	exchange.pgm_tkt_user = user + 1;
	return pgm_atomic_compare_and_exchange32 (&ticket->pgm_tkt_data32, exchange.pgm_tkt_data32, comparand.pgm_tkt_data32);
}

static inline void pgm_ticket_lock (pgm_ticket_t* ticket) {
	const uint16_t user = pgm_atomic_fetch_and_inc16 (&ticket->pgm_tkt_user);
#ifdef _WIN32
	unsigned spins = 0;
	while (ticket->pgm_tkt_ticket != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			SwitchToThread();
		else
			YieldProcessor();			/* hyper-threading pause */
#elif defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
/* GCC atomics */
	unsigned spins = 0;
	while (ticket->pgm_tkt_ticket != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			sched_yield();
		else
			__asm volatile ("pause" ::: "memory");	/* hyper-threading pause */
#else
	while (ticket->pgm_tkt_ticket != user)
		sched_yield();
#endif
}

static inline void pgm_ticket_unlock (pgm_ticket_t* ticket) {
	pgm_atomic_inc16 (&ticket->pgm_tkt_ticket);
}

/* read-write ticket spinlocks */

static inline void pgm_rwticket_init (pgm_rwticket_t* rwticket) {
	rwticket->pgm_rwtkt_data32 = 0;
}

static inline void pgm_rwticket_free (pgm_rwticket_t* rwticket) {
/* nop */
	(void)rwticket;
}

static inline void pgm_rwticket_reader_lock (pgm_rwticket_t* rwticket) {
	const uint8_t user = pgm_atomic_fetch_and_inc8 (&rwticket->pgm_rwtkt_user);
#ifdef _WIN32
	unsigned spins = 0;
	while (rwticket->pgm_rwtkt_read != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			SwitchToThread();
		else
			YieldProcessor();
#elif defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
	unsigned spins = 0;
	while (rwticket->pgm_rwtkt_read != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			sched_yield();
		else
			__asm volatile ("pause" ::: "memory");
#else
	while (rwticket->pgm_rwtkt_read != user)
		sched_yield();
#endif
	pgm_atomic_inc8 (&rwticket->pgm_rwtkt_read);
}

static inline bool pgm_rwticket_reader_trylock (pgm_rwticket_t* rwticket) {
	const uint8_t user = rwticket->pgm_rwtkt_user;
	pgm_rwticket_t exchange, comparand;
	exchange.pgm_rwtkt_data32 = comparand.pgm_rwtkt_data32 = 0;
	exchange.pgm_rwtkt_write = comparand.pgm_rwtkt_write = rwticket->pgm_rwtkt_write;
	comparand.pgm_rwtkt_user = comparand.pgm_rwtkt_read = user;
	exchange.pgm_rwtkt_user = exchange.pgm_rwtkt_read = user + 1;
	return pgm_atomic_compare_and_exchange32 (&rwticket->pgm_rwtkt_data32, exchange.pgm_rwtkt_data32, comparand.pgm_rwtkt_data32);
}

static inline void pgm_rwticket_reader_unlock(pgm_rwticket_t* rwticket) {
	pgm_atomic_inc8 (&rwticket->pgm_rwtkt_write);
}

/* users++
 */

static inline void pgm_rwticket_writer_lock (pgm_rwticket_t* rwticket) {
	const uint8_t user = pgm_atomic_fetch_and_inc8 (&rwticket->pgm_rwtkt_user);
#ifdef _WIN32
	unsigned spins = 0;
	while (rwticket->pgm_rwtkt_write != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			SwitchToThread();
		else
			YieldProcessor();
#elif defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
	unsigned spins = 0;
	while (rwticket->pgm_rwtkt_write != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			sched_yield();
		else
			__asm volatile ("pause" ::: "memory");
#else
	while (rwticket->pgm_rwtkt_write != user)
		sched_yield();
#endif
}

/* users++
 */

static inline bool pgm_rwticket_writer_trylock (pgm_rwticket_t* rwticket) {
	const uint8_t user = rwticket->pgm_rwtkt_user;
	pgm_rwticket_t exchange, comparand;
	exchange.pgm_rwtkt_data32 = comparand.pgm_rwtkt_data32 = 0;
	exchange.pgm_rwtkt_read = comparand.pgm_rwtkt_read = rwticket->pgm_rwtkt_read;
	exchange.pgm_rwtkt_write = comparand.pgm_rwtkt_user = comparand.pgm_rwtkt_write = user;
	exchange.pgm_rwtkt_user = user + 1;
	return pgm_atomic_compare_and_exchange32 (&rwticket->pgm_rwtkt_data32, exchange.pgm_rwtkt_data32, comparand.pgm_rwtkt_data32);
}

/* read-ticket++, write-ticket++;
 */

static inline void pgm_rwticket_writer_unlock (pgm_rwticket_t* rwticket) {
	pgm_rwticket_t t;
	t.pgm_rwtkt_data16 = pgm_atomic_read16 (&rwticket->pgm_rwtkt_data16);
	t.pgm_rwtkt_write++;
	t.pgm_rwtkt_read++;
	pgm_atomic_write16 (&rwticket->pgm_rwtkt_data16, t.pgm_rwtkt_data16);
}

PGM_END_DECLS

#endif /* __PGM_IMPL_TICKET_H__ */
