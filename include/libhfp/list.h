/* -*- C++ -*- */
/*
 * C++ Embedded Linked List Implementation, originally part of
 * Network Daemon Toolkit
 * Copyright (C) 2002-2003 Sam Revitch <samr7@cs.washington.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if !defined(__LIBHFP_LIST_H__)
#define __LIBHFP_LIST_H__

#include <sys/types.h>
#include <assert.h>

namespace libhfp {
/**
 * @defgroup embedded_linked_list Embedded Linked List Package
 *
 *	The Linked-List package provides two flavors of linked-lists.
 * Both are implemented using embedded data structures, such that all
 * memory required by the list package is implicitly allocated and
 * managed by its clients.
 *
 *	First, the ListItem object, a doubly-linked list, supports
 * unrestricted O(1) insertions and removals.  Very simple and
 * versatile.
 *
 *	Second, the SListItem object, a singly-linked list, supports
 * strict LIFO operations, where only succeeding elements can be
 * removed.  SListItem is less applicable than ListItem, but requires
 * less memory.
 *
 *	The Linked-List package also provides basic sorting
 * algorithms, including Merge Sort and Radix Sort.  These are
 * provided in the ListMergeSort, ListRadixSort, and SListRadixSort
 * functor classes.
 *
 *	If items require infrequent sorting by one of several
 * attributes, consider using ListItem to list items, and MergeSort
 * or RadixSort to perform the infrequent sorting.  If items are
 * frequently searched or enumerated in order, and sorted by a single
 * attribute, consider using the BinTree class.  For twice the amount
 * of space in each item, and more expensive insertions and removals,
 * it provides incremental sorting and bidirectional enumerability.
 *
 * To use a List:
 *	-# Embed a ListItem or SListItem object in each application-
 *	   specific object participating as a list item (call the
 *	   member ListMember).
 *	-# Create a ListItem in the managing object, e.g. the queue
 *	   head if handling lists of packets (call it XList).  This
 *	   is used as the head, and will point to the first and last
 *	   inserted items.
 *	-# To append an item, use
 *	   XList.AppendItem(ItemPointer->ListMember)
 *	-# To remove an item, use ItemPointer->ListMember.Unlink().
 */

/// Embedded Doubly-Linked-List Class
/**
 * @ingroup embedded_linked_list
 *
 * ListItem is a cheap knockoff of the Linux struct list_head embeddable
 * doubly linked list structure.  It works almost identically.  All
 * methods are bonehead simple and do very little work, and are
 * declared inline.
 *
 *	ListItem implements a doubly-linked-list, with O(1) insertions
 * and removals.  Very simple and versatile.  Basic sorting algorithms,
 * including MergeSort and RadixSort, are also provided through the
 * ListMergeSort and ListRadixSort classes.
 *
 *	ListItem includes methods to make it useful as both an item
 * structure and a list head structure.
 *
 *	Lists may also be used in a loop fashion without a start
 * pointer, although it can be trickier.
 *
 * @note On 32-bit platforms, a ListItem incurrs 8 bytes of overhead.
 */
class ListItem {
 public:

	/// List member pointer
	ListItem *next, *prev;

	/// Regular Constructor
	/**
	 * Initializes the list as a circular loop of one.
	 */
	inline ListItem(void)
		: next(this), prev(this) {}

	/// Is list empty?
	/**
	 * Checks whether the list is an empty circular loop.
	 *
	 * @retval true next points to this and prev points to this
	 * @retval false The list is linked with other ListItems.
	 */
	inline bool Empty(void) const {
		return (next == this);
	}

	/// Reinitialize list item
	/**
	 * Be careful with this, as it can corrupt things if the
	 * item is linked!
	 */
	inline void Reinitialize(void) { next = prev = this; }

	/// Unlink and reinitialize
	/**
	 * Unlink the ListItem from whatever list it is linked
	 * to and reinitialize it to an empty circular list.
	 */
	inline void Unlink(void) {
		next->prev = prev;
		prev->next = next;
		next = this;
		prev = this;
	}

	/// Unlink without reinitializing
	/**
	 * Unlink the ListItem from whatever list it is linked
	 * to.  Does not reinitialize the list.  Empty() will not
	 * return the correct value.  Use carefully!
	 */
	inline void UnlinkOnly(void) {
		next->prev = prev;
		prev->next = next;
	}

	/// Unlink and reinitialize successor
	/**
	 * Unlink the successor element, reinitialize it, and return
	 * a pointer.
	 */
	inline ListItem *UnlinkNext(void) {
		ListItem *listp = next;
		next = listp->next;
		listp->next->prev = this;
		listp->next = listp->prev = listp;
		return listp;
	}

	/// Unlink successor without reinitializing
	/**
	 * Similar to UnlinkNext(), but skips reinitializing the
	 * unlinked list.
	 */
	inline ListItem *UnlinkNextOnly(void) {
		ListItem *listp = next;
		next = listp->next;
		listp->next->prev = this;
		return listp;
	}

	/// Insert a single item immediately after this element
	/**
	 * Links another ListItem to this ListItem's circular list,
	 * such that the new ListItem becomes this element's
	 * immediate successor.  If this element is considered to be
	 * the head of a list, whereas item is considered to be an
	 * item, then item is prepended to the list headed by this.
	 *
	 * @param item ListItem to be inserted.
	 * @warning Blindly overwrites pointers in item.  If item
	 *	is linked to another list, that list will become
	 *	corrupt.
	 */
	inline void PrependItem(ListItem &item) {
		item.prev = this;
		item.next = next;
		next->prev = &item;
		next = &item;
	}

	/// Insert a single item immediately before this element
	/**
	 * Links another ListItem to this ListItem's circular list,
	 * such that the new ListItem becomes this element's
	 * immediate predecessor.  If this element is considered to
	 * be the head of a list, whereas item is considered to be an
	 * item, then item is appended to the list headed by this.
	 *
	 * @param item ListItem to be inserted.
	 * @warning Blindly overwrites pointers in item.  If item
	 *	is linked to another list, that list will become
	 *	corrupt.
	 */
	inline void AppendItem(ListItem &item) {
		item.prev = prev;
		item.next = this;
		prev->next = &item;
	        prev = &item;
	}

	/// Insert a sublist immediately after this element
	/**
	 * Unlinks a chain of ListItem structures from whatever list
	 * they might be linked onto, and inserts them immediately
	 * after the subject ListItem.
	 *
	 * @param before_first ListItem on the source list immediately
	 *	preceding the starting ListItem of the chain to splice.
	 * @param last Final ListItem on the source list of the chain
	 *	to splice.
	 */
	inline void SpliceAfter(ListItem &before_first, ListItem &last) {
		ListItem *tmp = next;
		assert(&before_first != &last);		// Invalid splice
		next = before_first.next;
		next->prev = this;
		before_first.next = last.next;
		before_first.next->prev = &before_first;
		last.next = tmp;
		tmp->prev = &last;
	}

	/// Insert a sublist immediately before this element
	/**
	 * Unlinks a chain of ListItem structures from whatever list
	 * they might be linked onto, and inserts them immediately
	 * before the subject ListItem.
	 *
	 * @param before_first ListItem on the source list immediately
	 *	preceding the starting ListItem of the chain to splice.
	 * @param last Final ListItem on the source list of the chain
	 *	to splice.
	 */
	inline void SpliceBefore(ListItem &before_first, ListItem &last)
		{ prev->SpliceAfter(before_first, last); }

	/// Prepends all elements of a list into this
	/**
	 * Splices all items of source onto the beginning of the
	 * subject list.  The parameter source is reinitialized as
	 * part of this operation.  Upon return, source will be
	 * empty.
	 *
	 * @param source List from which items should be transferred.
	 */
	inline void PrependItemsFrom(ListItem &source) {
		if (!source.Empty()) {
			SpliceAfter(source, *source.prev);
		}
	}

	/// Appends all elements of a list into this
	/**
	 * Splices all items of source onto the end of the subject
	 * list.  The parameter source is reinitialized as part of
	 * this operation.  Upon return, source will be empty.
	 *
	 * @param source List from which items should be transferred.
	 */
	inline void AppendItemsFrom(ListItem &source) {
		if (!source.Empty()) {
			SpliceBefore(source, *source.prev);
		}
	}

	/// Compute the length of a list
	/**
	 * Determines how many items are linked into a list,
	 * not including the ListItem it is called on.
	 */
	inline int Length() const {
		int iter = 0;
		ListItem *listp = next;
		while (listp != this) {
			assert(listp->next->prev == listp);
			listp = listp->next;
			iter++;
		}
		return iter;
	}

	/// Deprecated
	inline void SpliceInto(ListItem &head, ListItem &cut_start) {
		ListItem *tmp = next;
		next = &cut_start;
		cut_start.prev->next = &head;
		head.prev->next = tmp;
		tmp->prev = head.prev;
		head.prev = cut_start.prev;
		cut_start.prev = this;
	}
	/// Deprecated
	inline void SpliceIntoEnd(ListItem &head, ListItem &cut_start) {
		ListItem *tmp = prev;
		prev = head.prev;
		head.prev->next = this;
		cut_start.prev->next = &head;
		head.prev = cut_start.prev;
		cut_start.prev = tmp;
		tmp->next = &cut_start;
	}

 private:
	/* Disable copy constructor and assignment */
	inline ListItem(const ListItem &)
		: next(this), prev(this) { assert(0); }
	inline ListItem &operator=(ListItem &)
		{ assert(0); return *this; }
};


/// Get a pointer to a structure containing a given ListItem.
/**
 * @ingroup embedded_linked_list
 *
 * @param memberptr	Pointer to the embedded ListItem.
 * @param type		Name of structure type containing the list.
 * @param member	Name of structure member of the ListItem.
 *
 * For exmaple, if you have a:
 *
 * @code
 *    	struct some_struct {
 *		int stuff.
 *		...
 *		ListItem mylist;
 *		...
 *	}
 * @endcode
 *
 * and have a pointer to its mylist member, and need a pointer to
 * the struct some_struct, just use:
 *
 * @code
 *	GetContainer(listptr, struct some_struct, mylist)
 * @endcode
 */

/*
 * The most generic version of the container resolution function.
 * Makes use of C++ templates to do extra type-checking.
 * It can also return an ancestor type of ContainerT in which MemberT
 * was first defined, so we static_cast<> its result type to the
 * desired container type.
 */
#if 1
#define GetContainer(memberptr, type, member) \
static_cast<type*>(__GetContainer(memberptr, &type::member))

template <typename ContainerT, typename MemberT>
inline ContainerT *__GetContainer(const MemberT *ptr,
				  const MemberT ContainerT::* memberptr) {
        return reinterpret_cast<ContainerT *>(reinterpret_cast<size_t>(ptr) -
					      reinterpret_cast<size_t>
		      (&(reinterpret_cast<ContainerT*>(0)->*memberptr)));
}

/*
 * Dumbed-down version.  Uses C-style typecasts and results in less
 * compile-time type checking.
 */
#else
#define GetContainer(memberptr, type, member) \
        ((type *) (((unsigned long) memberptr) - (unsigned long) (&(((type *) 0)->member))))
#endif


/// Forward list iterator
/**
 * @ingroup embedded_linked_list
 *
 * For-loop iterating over all members of a list in forward order.
 *
 * @param listp		Iterator variable, assigned to the ListItem
 *			under consideration for each iteration.
 * @param head		List head.  Iteration starts at the beginning
 *			of this, and stops when listp == head.
 */
#define ListForEach(listp, head) for (listp = (head)->next; listp != (head); listp = listp->next)


/// Reverse list iterator
/**
 * @ingroup embedded_linked_list
 *
 * For-loop iterating over all members of a list in forward order.
 *
 * @param listp		Iterator variable, assigned to the ListItem
 *			under consideration for each iteration.
 * @param head		List head.  Iteration starts at the end of
 *			this, and stops when listp == head.
 */
#define ListForEachReverse(listp, head) for (listp = (head)->prev; listp != (head); listp = listp->prev)


/// Merge-sort functor for ListItem lists
/**
 * @ingroup embedded_linked_list
 *
 *	Templated class with a single static function Sort(), used for
 * sorting lists of any type of object, using the MergeSort algorithm.
 * This is useful for sorting objects for which direct comparison is
 * possible but no numeric value can easily be associated with each.
 * Note the restrictions below.
 *	This well-known algorithm exhibits O(N * log2(N)) performance
 * for sorting a list of N objects, guaranteed in all cases.
 *
 * @param CompFunc	Comparison function container-class.
 *
 * The CompFunc container-class must contain the following members:
 *
 *    - typedef X param_t
 *	      - Parameter type to Sort().
 *
 *    - bool CompItems(ListItem *one, ListItem *two, param_t param)
 *	      - Comparison function.  Compares the objects associated
 *		with two ListItems.  The function must be conscious
 *		of what type of structure the ListItems are part of.
 *		Should return:
 *		      - true	one comes before two.
 *		      - false  	two comes before one.
 *		This member-function must be static, and for best
 *		results, inline.
 */
template <typename CompFunc>
class ListMergeSort {
public:
	typedef typename CompFunc::param_t param_t;

	/// Invokes the MergeSort algorithm
	/**
	 *	Sorts a list of ListItems.  Operates recursively
	 * and can create a large call stack for long lists of
	 * objects to be sorted.
	 *
	 *	MergeSort must divide the list of items into two, so
	 * the length of the list must be known.
	 *
	 * @param list		List of items.
	 * @param itemcount	Number of items in list.
	 * @param parameter	Argument to the comparison function.
	 */
	static void Sort(ListItem &list, int itemcount, param_t parameter) {
		ListItem listA, listB, *listp;
		int i;

		assert(list.Length() == itemcount);

		/*
		 * Empty lists and singletons are by nature already
		 * sorted and will screw us up.
		 */
		if (list.next == list.prev)
			return;

		/*
		 * Divide the contents of list *mostly evenly*
		 * between listA and listB.
		 */
		listA.PrependItemsFrom(list);
		listp = listA.next;
		for (i = 0; i < ((itemcount / 2) - 1); i++)
			listp = listp->next;
		listB.SpliceAfter(*listp, *listA.prev);
		i++;

		/*
		 * Run mergesort on both lists individually.
		 */
		Sort(listA, i, parameter);
		Sort(listB, itemcount - i, parameter);

		/*
		 * Merge the results back together
		 */

		while (!listA.Empty() &&
		       !listB.Empty()) {

			if (CompFunc::CompItems(listA.next,
						listB.next,
						parameter)) {
				listp = listA.UnlinkNextOnly();
				list.AppendItem(*listp);
			} else {
				listp = listB.UnlinkNextOnly();
				list.AppendItem(*listp);
			}
		}

		list.AppendItemsFrom(listA);
		list.AppendItemsFrom(listB);

		assert(list.Length() == itemcount);
	}

	/// Alternative overload of Sort() which determines length
        /**
	 * Version of Sort() that requires no length parameter.
	 *
	 * @param list		List of items.
	 * @param parameter	Argument to the comparison function.
	 */
	static inline void Sort(ListItem &list, param_t parameter) {
		Sort(list, list.Length(), parameter);
	}

};


/// Radix-sort functor for ListItem lists
/**
 * @ingroup embedded_linked_list
 *
 *	Templated class with a single static function Sort(), used for
 * sorting lists of any type of object, using the RadixSort algorithm.
 * This is useful for sorting objects for which an integer sort value
 * can be assigned to each.  Note the restrictions below.
 *	This algorithm exhibits O(N) performance for sorting a list
 * of N objects, guaranteed in all cases.  It is usually preferable to
 * MergeSort in cases where it can be used.
 *
 * @param CompFunc	Comparison function container-class.
 *
 * The CompFunc container-class must contain the following members:
 *
 *     - typedef X value_t
 *	      - Type of value being sorted on.  Must be an unsigned
 *		integral type.  Typically this value is unsigned int,
 *		although it can be larger or smaller depending on the
 *		use.
 *
 *     - const unsigned int m_nbucket_bits
 *	      - Number of bits in the bucket address space.  Directly
 *		determines the number of buckets to be used, which is
 *		(1 << m_nbucket_bits).  A safe value is 4.  Larger
 *		values improve performance for large input lists, but
 *		increase fixed overhead.
 *
 *     - typedef X param_t
 *	      - Parameter type to Sort().
 *
 *     - value_t ItemValue(ListItem *item, param_t param)
 *	      - Value-determining function for a given list item.
 *		This member-function must be static, and for best
 *		results, inline.
 */
template <typename CompFunc>
class ListRadixSort {
 private:
	typedef typename CompFunc::value_t value_t;
	typedef typename CompFunc::param_t param_t;

	enum {
		c_nbucket_bits = CompFunc::m_nbucket_bits,
		c_nbuckets = (1 << CompFunc::m_nbucket_bits)
	};

 public:
	/// Invokes the RadixSort algorithm
	/**
	 * @param list		List of items.
	 * @param parameter	Argument to the comparison function.
	 */
	static void Sort(ListItem &list, param_t parameter) {
		ListItem *listp;
		ListItem buckets[c_nbuckets];
		unsigned int i;
		unsigned int nbits_left;

		/*
		 * Iterate until we run out of bits in the key space.
		 * Sort the list by the lower bits first.
		 * Note that on each succeeding pass, list is in order
		 */
		for (nbits_left = 0;
		     nbits_left < (sizeof(value_t) << 3);
		     nbits_left += c_nbucket_bits) {

			/*
			 * First move all elements from the input list
			 * into an appropriate bucket.
			 */
			while (!list.Empty()) {

				listp = list.UnlinkNextOnly();
				i = static_cast<unsigned int>
					((CompFunc::ItemValue(listp, parameter)
					  >> nbits_left)
					 & (c_nbuckets - 1));
				buckets[i].AppendItem(*listp);
			}

			/*
			 * Now concatenate the list of each bucket
			 * back into the input list, IN ORDER.
			 */
			for (i = 0; i < c_nbuckets; i++) {
				list.AppendItemsFrom(buckets[i]);
			}

			/*
			 * Isn't this simple?  It's so damn fast.
			 */
		}
	}

	/*
	 * To match the standard Sort(list, itemcount, param) prototype,
	 * just drop the itemcount.
	 */
	static inline void Sort(ListItem &list, int itemcount,
				param_t parameter)
		{ Sort(list, parameter); }
};


/// Embedded Singly-linked list Class
/**
 * @ingroup embedded_linked_list
 *
 * Low-overhead embedded singly-linked-list.  Enforces strict LIFO
 * insertion/removal semantics, and is less powerful and capable than
 * ListItem, its doubly-linked counterpart.
 *
 * Like the ListItem, SListItem can be used as both a list head and
 * a list item, although lists utilizing SListItem as the head
 * structure are restricted to LIFO operations.
 *
 * SListQueue can also be used as the list head structure.  This
 * structure includes a tail pointer, and is capable of all splice
 * and insert operations.
 *
 * Supports sort functors.
 *
 * @sa SListQueue, SListRadixSort
 */
class SListItem {
 public:
	/// List member pointer
	SListItem *next;

	/// Regular Constructor
	/**
	 * Initializes the list as a circular loop of one.
	 */
	inline SListItem() 
		: next(this) {}

	/// Is list empty?
	/**
	 * Checks whether the list is an empty circular loop.
	 *
	 * @retval true next points to this
	 * @retval false The link is linked with other SListItems.
	 */
	inline bool Empty() const { return (next == this); }

	/// Reinitialize list item
	/**
	 * Reinitializes the ListItem as a singleton circular list.
	 *
	 * @warning Use with caution - a list will become corrupt
	 *	if the item is linked!
	 */
	inline void Reinitialize(void) { next = this; }

	/// Unlink and reinitialize successor
	/**
	 * Unlinks and reinitializes the successor of this entry in
	 * the list.
	 *
	 * @return Pointer to former successor, or NULL if empty.
	 */
	inline SListItem *UnlinkNext() {
		SListItem *tmp;
		if (next == this)
			return 0;
		tmp = next;
		next = next->next;
		tmp->next = tmp;
		return tmp;
	}

	/// Unlink successor without reinitialization
	/**
	 * Unlinks the successor of this entry in the list.
	 * Marginally more efficient than UnlinkNext().
	 *
	 * @return Pointer to former successor, or NULL if empty.
	 * @warning The former successor will have a dangling next
	 *	pointer.
	 */
	inline SListItem *UnlinkNextOnly() {
		SListItem *tmp;
		if (next == this)
			return 0;
		tmp = next;
		next = next->next;
		return tmp;
	}

	/// Insert a single item immediately after this element
	/**
	 * Links another SListItem to this SListItem's circular list,
	 * such that the new SListItem becomes this element's
	 * immediate successor.  If this element is considered to be
	 * the head of a list, and item is considered to be an item,
	 * then the item is prepended to the list headed by this.
	 *
	 * @param item SListItem to be inserted.
	 * @warning Blindly overwrites pointers in item.  If item
	 *	is linked to another list, that list will become
	 *	corrupt.
	 */
	inline void PrependItem(SListItem &item) {
		item.next = next;
		next = &item;
	}

	/// Insert a sublist immediately after this element
	inline void SpliceAfter(SListItem &before_first, SListItem &last) {
		SListItem *tmp = next;
		assert(&before_first != &last);		// Invalid splice
		next = before_first.next;
		before_first.next = last.next;
		last.next = tmp;
	}

	/// Compute the length of a list
	/**
	 * Determines how many items are linked into a list,
	 * not including the ListItem it is called on.
	 */
	inline int Length() const {
		int iter = 0;
		SListItem *listp = next;
		while (listp != this) {
			listp = listp->next;
			iter++;
		}
		return iter;
	}

	/// Alias for PrependItem()
	inline void Push(SListItem &item) { PrependItem(item); }
	/// Alias for UnlinkNext()
	inline SListItem *Pop(void) { return UnlinkNext(); }

 private:
	/* Disable copy constructor and assignment */
	inline SListItem(const SListItem &)
		: next(this) { assert(0); }
	inline SListItem &operator=(SListItem &)
		{ assert(0); return *this; }
};


/// Queue head for SListItem elements supporting FIFO operations
/**
 * @ingroup embedded_linked_list
 *
 * Offers the efficiency tradeoff of having a double-pointer list
 * head, but single-pointer elements.  This can be used as a memory-
 * saving alternative to ListItem for cases where the following
 * functionality is not needed:
 *	- Removal of arbitrary elements, ListItem::Unlink().
 *	- Removal of predecessor elements, prev->Unlink().
 *	- Reverse iteration, ListForEachReverse.
 */
class SListQueue {
 public:
	SListItem		m_Head;
	SListItem		*tail;

	/// Default constructor
	inline SListQueue(void) : m_Head(), tail(&m_Head) {}

	/// Default destructor -- asserts that queue is empty
	inline ~SListQueue(void) { assert(tail == &m_Head); }

	/// Is queue empty?
	/**
	 * Checks whether the queue is an empty circular loop.
	 *
	 * @retval true next points to this and prev points to this
	 * @retval false The list is linked with other ListItems.
	 */
	inline bool Empty(void) const { return m_Head.Empty(); }

	/// Reinitialize queue head
	/**
	 * Reinitializes the SListQueue as a singleton circular list.
	 *
	 * @warning Use with caution - any elements linked into the
	 *   	queue at the time this method is called will be
	 *	improperly unlinked, and the list pointers of such
	 *	will certainly become corrupt.
	 */
	inline void Reinitialize(void) {
		m_Head.Reinitialize();
		tail = &m_Head;
	}

	/// Insert a single item at the front of the queue
	/**
	 * Links an SListItem to this SListQueue's circular list,
	 * such that item becomes the first item on the queue, or
	 * the immediate successor of the queue head element.
	 *
	 * @param item SListItem to be inserted.
	 * @warning Blindly overwrites pointers in item.  If item
	 *	is linked to another list, that list will become
	 *	corrupt.
	 */
	inline void PrependItem(SListItem &item) {
		if (m_Head.Empty()) {
			assert(tail == &m_Head);
			tail = &item;
		}
		m_Head.PrependItem(item);
	}

	/// Insert a single item at the back of the queue
	/**
	 * Links an SListItem to this SListQueue's circular list,
	 * such that item becomes the last item on the queue, or
	 * the immediate predecessor of the queue head element.
	 *
	 * @param item SListItem to be inserted.
	 * @warning Blindly overwrites pointers in item.  If item
	 *	is linked to another list, that list will become
	 *	corrupt.
	 */
	inline void AppendItem(SListItem &item) {
		tail->PrependItem(item);
		tail = &item;
	}

	/// Insert a sublist at the head of the queue
	/**
	 * Unlinks a chain of SListItem structures from whatever list
	 * they might be linked onto, and prepends them to the
	 * beginning of the queue.
	 *
	 * @param before_first SListItem on the source list
	 *	immediately preceding the starting SListItem of the
	 *	chain to splice.
	 * @param last Final SListItem on the source list of the
	 *	chain to splice.
	 */
	inline void SpliceAfter(SListItem &before_first, SListItem &last) {
		if (m_Head.Empty()) {
			assert(tail == &m_Head);
			tail = &last;
		}
		m_Head.SpliceAfter(before_first, last);
	}

	/// Insert a sublist at the tail of the queue
	/**
	 * Unlinks a chain of SListItem structures from whatever list
	 * they might be linked onto, and appends them to the end of
	 * the queue.
	 *
	 * @param before_first SListItem on the source list
	 *	immediately preceding the starting SListItem of the
	 *	chain to splice.
	 * @param last Final SListItem on the source list of the
	 *	chain to splice.
	 */
	inline void SpliceBefore(SListItem &before_first, SListItem &last) {
		tail->SpliceAfter(before_first, last);
		tail = &last;
	}

	/// Prepends all elements of a list into this
	/**
	 * Splices all items of source onto the beginning of the
	 * subject queue.  The parameter source is reinitialized as
	 * part of the operation.  Upon return, source will be empty.
	 *
	 * @param source List from which items should be transferred.
	 */
	inline void PrependItemsFrom(SListQueue &source) {
		if (!source.Empty()) {
			SpliceAfter(source.m_Head, *source.tail);
		}
	}

	/// Appends all elements of a list into this
	/**
	 * Splices all items of source onto the end of the subject
	 * queue.  The parameter source is reinitialized as part of
	 * this operation.  Upon return, source will be empty.
	 *
	 * @param source List from which items should be transferred.
	 */
	inline void AppendItemsFrom(SListQueue &source) {
		if (!source.Empty()) {
			SpliceBefore(source.m_Head, *source.tail);
		}
	}

	/// Unlink and reinitialize successor
	/**
	 * Unlinks and reinitializes the successor of this entry in
	 * the list.
	 *
	 * @return Pointer to former successor, or NULL if empty.
	 */
	inline SListItem *UnlinkNext(void) {
		SListItem *ret = m_Head.UnlinkNext();
		if (tail == ret) {
			assert(m_Head.Empty());
			tail = &m_Head;
		}
		return ret;
	}

	/// Unlink successor without reinitialization
	/**
	 * Unlinks the successor of this entry in the list.
	 * Marginally more efficient than UnlinkNext().
	 *
	 * @return Pointer to former successor, or NULL if empty.
	 * @warning The former successor will have a dangling next
	 *	pointer.
	 */
	inline SListItem *UnlinkNextOnly(void) {
		SListItem *ret = m_Head.UnlinkNextOnly();
		if (tail == ret) {
			assert(m_Head.Empty());
			tail = &m_Head;
		}
		return ret;
	}

	/// Alias for PrependItem()
	inline void Push(SListItem &item) { PrependItem(item); }
	/// Alias for UnlinkNext()
	inline SListItem *Pop(void) { return UnlinkNext(); }
	/// Alias for AppendItem()
	inline void Enqueue(SListItem &item) { AppendItem(item); }
	/// Alias for UnlinkNext()
	inline SListItem *Dequeue(void) { return UnlinkNext(); }

 private:
	/* Disable copy constructor and assignment */
	inline SListQueue(const SListQueue &)
		: m_Head(), tail(&m_Head) { assert(0); }
	inline SListQueue &operator=(SListQueue &)
		{ assert(0); return *this; }
};


/// Radix-sort functor for SListItem lists
/**
 * @ingroup embedded_linked_list
 *
 *	This object has semantics identical to ListRadixSort.  See
 * the comments in that class for a better explanation of how it
 * works.
 *
 * @sa ListRadixSort
 */
template <typename CompFunc>
class SListRadixSort {
 private:
	typedef typename CompFunc::value_t value_t;
	typedef typename CompFunc::param_t param_t;

	enum {
		c_nbucket_bits = CompFunc::m_nbucket_bits,
		c_nbuckets = (1 << CompFunc::m_nbucket_bits)
	};

	static void Sort(SListItem &list, SListItem *tail, param_t parameter) {
		SListItem *listp;
		SListQueue q;
		SListQueue buckets[c_nbuckets];
		unsigned int i;
		unsigned int nbits_left;

		/*
		 * Iterate until we run out of bits in the key space.
		 * Sort the list by the lower bits first.
		 * Note that on each succeeding pass, list is in order
		 */
		for (nbits_left = 0;
		     nbits_left < (sizeof(value_t) << 3);
		     nbits_left += c_nbucket_bits) {
			while (!list.Empty()) {
				listp = list.UnlinkNextOnly();
				i = static_cast<unsigned int>
					((CompFunc::ItemValue(listp, parameter)
					  >> nbits_left)
					 & (c_nbuckets - 1));
				buckets[i].AppendItem(*listp);
			}
			q.Reinitialize();
			for (i = 0; i < c_nbuckets; i++) {
				q.AppendItemsFrom(buckets[i]);
			}
			list.next = q.m_Head.next;
		}
		if (tail) { *tail = q.tail; }
	}

public:
	static inline void Sort(SListItem &list, param_t parameter)
		{ Sort(list, 0, parameter); }
	static inline void Sort(SListQueue &queue, param_t parameter)
		{ Sort(queue.m_Head, &queue.tail, parameter); }

	static inline void Sort(SListItem &list, int itemcount,
				param_t parameter)
		{ Sort(list, parameter); }
	static inline void Sort(SListQueue &queue, int itemcount,
				param_t parameter)
		{ Sort(queue, parameter); }
};

}  /* namespace libhfp */
#endif /* !defined(__LIBHFP_LIST_H__) */
