#ifndef BWT_MAP_H
#define BWT_MAP_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <fstream>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <map>
#include <vector>
#include <cassert>

#include <boost/shared_ptr.hpp>

#ifdef HAVE_HTSLIB
#include <htslib/hts.h>
#include <htslib/hfile.h>
#include <htslib/cram.h>
#include <htslib/bgzf.h>
#include <htslib/sam.h>
#else
#include <bam/sam.h>
#endif

#include "common.h"
#include "multireads.h"

using namespace std;

/*
 *  hits.h
 *  Cufflinks
 *
 *  Created by Cole Trapnell on 3/23/09.
 *  Copyright 2009 Cole Trapnell. All rights reserved.
 *
 */

enum CuffStrand { CUFF_STRAND_UNKNOWN = 0, CUFF_FWD = 1, CUFF_REV = 2, CUFF_BOTH = 3 };


enum CigarOpCode 
{ 
	MATCH = BAM_CMATCH, 
	INS = BAM_CINS, 
	DEL = BAM_CDEL, 
	REF_SKIP = BAM_CREF_SKIP,
	SOFT_CLIP = BAM_CSOFT_CLIP, 
	HARD_CLIP = BAM_CHARD_CLIP, 
	PAD = BAM_CPAD
};

struct CigarOp
{
	CigarOp(CigarOpCode o, uint32_t l) : opcode(o), length(l) {}
	CigarOpCode opcode : 3;
	uint32_t length : 29;
	
	bool operator==(const CigarOp& rhs) const { return opcode == rhs.opcode && length == rhs.length; }
	
};

typedef uint64_t InsertID;
typedef uint64_t RefID;

extern int num_deleted;

/*  Stores the information from a single record of the bowtie map. A given read
    may have many of these.
*/
struct ReadHit
{
	ReadHit() : 
		_ref_id(0),
		_insert_id(0),
        _base_mass(1.0),
        _edit_dist(0xFFFFFFFF),
		_num_hits(1)
    {
        num_deleted++;
    }
	
	ReadHit(RefID ref_id,
			InsertID insert_id, 
			int left, 
			int read_len, 
			CuffStrand source_strand,
			RefID partner_ref,
			int partner_pos,
			unsigned int edit_dist,
			int num_hits,
            float base_mass,
            uint32_t sam_flag) :
		_ref_id(ref_id),
		_insert_id(insert_id), 
		_left(left), 
		_partner_ref_id(partner_ref),
		_partner_pos(partner_pos),
		_cigar(vector<CigarOp>(1,CigarOp(MATCH,read_len))),
		_source_strand(source_strand),
        _base_mass(base_mass),
        _edit_dist(edit_dist),
		_num_hits(num_hits),
        _sam_flag(sam_flag)
	{
		assert(_cigar.capacity() == _cigar.size());
		_right = get_right();
        num_deleted++;
	}
    
	ReadHit(RefID ref_id,
			InsertID insert_id, 
			int left,  
			const vector<CigarOp>& cigar,
			CuffStrand source_strand,
			RefID partner_ref,
			int partner_pos, 
			unsigned int  edit_dist,
			int num_hits,
            float base_mass,
            uint32_t sam_flag) : 
		_ref_id(ref_id),
		_insert_id(insert_id), 	
		_left(left),
		_partner_ref_id(partner_ref),
		_partner_pos(partner_pos),
		_cigar(cigar),
		_source_strand(source_strand),
        _base_mass(base_mass),
        _edit_dist(edit_dist),
		_num_hits(num_hits),
        _sam_flag(sam_flag)
	{
		assert(_cigar.capacity() == _cigar.size());
		_right = get_right();
        num_deleted++;
	}
    
    ReadHit(const ReadHit& other)
    {
        _ref_id = other._ref_id;
		_insert_id = other._insert_id;
		_left = other._left;
		_partner_ref_id = other._partner_ref_id;
		_partner_pos = other._partner_pos;
		_cigar = other._cigar;
		_source_strand = other._source_strand;
		_num_hits = other._num_hits;
        _base_mass = other._base_mass;
        _edit_dist = other._edit_dist;
        _right = get_right();
        _sam_flag = other._sam_flag;
        num_deleted++;
    }
    
    ~ReadHit()
    {
        --num_deleted;
    }

	int read_len() const
	{
		int len = 0;
		for (size_t i = 0; i < _cigar.size(); ++i)
		{
			const CigarOp& op = _cigar[i];
			switch(op.opcode)
			{
				case MATCH:
				case INS:
				case SOFT_CLIP:
					len += op.length;
					break;
				default:
					break;
			}
		}
		
		return len;
	}
	
	bool contains_splice() const
	{
		for (size_t i = 0; i < _cigar.size(); ++i)
		{
				if (_cigar[i].opcode == REF_SKIP)
					return true;
		}
		return false;
	}
	
	bool is_singleton() const
	{ 
		return (partner_ref_id() == 0 ||
				partner_ref_id() != ref_id() ||
				abs(partner_pos() - left()) > max_partner_dist);
	}
	
	bool operator==(const ReadHit& rhs) const
	{
	    return (_insert_id == rhs._insert_id &&
	            _ref_id == rhs._ref_id &&
	            antisense_align() == rhs.antisense_align() &&
	            _left == rhs._left && 
	            _source_strand == rhs._source_strand &&
	            /* DO NOT USE ACCEPTED IN COMPARISON */
	            _cigar == rhs._cigar);
    }
	
	RefID ref_id() const				{ return _ref_id;			}
	InsertID insert_id() const			{ return _insert_id;		}
	
	RefID partner_ref_id() const		{ return _partner_ref_id;	}
	int	  partner_pos()	const			{ return _partner_pos;		}
	
	int left() const					{ return _left;				}
	int right() const					{ return _right;			}
	CuffStrand source_strand()	const	{ return _source_strand; }
	bool antisense_align() const		{ return _sam_flag & 0x10;	}
    bool is_first() const               { return _sam_flag & 0x40;  }	
	
	// Number of multi-hits for this read
	int num_hits() const				{ return _num_hits;			}
	
    // We are ignoring the _base_mass and re-calculating based on multi-hits
	double mass() const 
	{
		if (is_singleton())
			return 1.0/_num_hits;
		return 0.5 / _num_hits;
	}
	
	// For convenience, if you just want a copy of the gap intervals
	// for this hit.
	void gaps(vector<pair<int,int> >& gaps_out) const
	{
		gaps_out.clear();
		int pos = _left;
		for (size_t i = 0; i < _cigar.size(); ++i)
		{
			const CigarOp& op = _cigar[i];
			
			switch(op.opcode)
			{
				case REF_SKIP:
					gaps_out.push_back(make_pair(pos, pos + op.length - 1));
					pos += op.length;
					break;
                case SOFT_CLIP:
                    pos += op.length;
                    break;
                case HARD_CLIP:
                    break;
				case MATCH:
					pos += op.length;
					break;
                case INS:
                    pos -= op.length;
					break;
                case DEL:
                    pos += op.length;
					break;
				default:
					break;
			}
		}
	}
	
	const vector<CigarOp>& cigar() const { return _cigar; }
	
	bool contiguous() const 
	{ 
		return _cigar.size() == 1 && _cigar[0].opcode == MATCH;
	}
	
    unsigned int  edit_dist() const { return _edit_dist; }
    
    void trim(int trimmed_length);
    
	//const string& hitfile_rec() const { return _hitfile_rec; }
	//void hitfile_rec(const string& rec) { _hitfile_rec = rec; }
	
private:
	
	int get_right() const	
	{
		int r = _left;
		for (size_t i = 0; i < _cigar.size(); ++i)
		{
			const CigarOp& op = _cigar[i];
			
			switch(op.opcode)
			{
				case MATCH:
				case REF_SKIP:
                case SOFT_CLIP:
				case DEL:
					r += op.length;
					break;
                case INS:
                case HARD_CLIP:
				default:
					break;
			}
		}
		return r;			
	}
	
	RefID _ref_id;
	InsertID _insert_id;   // Id of the sequencing insert
	int _left;        // Position in the reference of the left side of the alignment
	int _right;
	
	RefID _partner_ref_id;  // Reference contig on which we expect the mate 
	int _partner_pos;     // Position at which we expect the mate of this hit
	
	
	vector<CigarOp> _cigar;
	
	CuffStrand _source_strand;    // Which strand the read really came from, if known
    float _base_mass;
    unsigned int  _edit_dist;            // Number of mismatches
	int _num_hits; // Number of multi-hits (1 by default)
    uint32_t _sam_flag;
	//string _hitfile_rec; // Points to the buffer for the record from which this hit came
};

class ReadTable
{
public:
	
	ReadTable() {}
	
	// This function should NEVER return zero
	InsertID get_id(const string& name)
	{
		uint64_t _id = hash_string(name.c_str());
		assert(_id);
		return _id;
	}
    
    // Calculate checksum
	InsertID get_cs(const string& name)
	{
        return string_checksum(name.c_str());
    }
    
private:
	
	// This is FNV-1, see http://en.wikipedia.org/wiki/Fowler_Noll_Vo_hash
	inline uint64_t hash_string(const char* __s)
	{
		uint64_t hash = 0xcbf29ce484222325ull;
		for ( ; *__s; ++__s)
		{
			hash *= 1099511628211ull;
			hash ^= *__s;
		}
		return hash;
	}
    
    
    inline uint64_t string_checksum(const char * s)
    {
        uint64_t c = 0;
        for ( ; *s; ++s)       
        {
            c += *s;
        }
        return c;
    }
};

class RefSequenceTable
{
public:
	
	typedef std::string Sequence;
	
	struct SequenceInfo
	{
		SequenceInfo(uint32_t _order, 
					 char* _name, 
					 Sequence* _seq) :
		observation_order(_order),
		name(_name),
		seq(_seq) {}
		uint32_t observation_order;
		char* name;
		Sequence* seq;
	};
	
	typedef map<string, RefID> IDTable;
	typedef map<RefID, SequenceInfo> InvertedIDTable;
	typedef InvertedIDTable::iterator iterator;
	typedef InvertedIDTable::const_iterator const_iterator;
	
	RefSequenceTable(bool keep_names, bool keep_seqs = false) : 
	_next_id(1), 
	_keep_names(keep_names) {}
	
	~RefSequenceTable()
	{
		for (InvertedIDTable::iterator itr = _by_id.begin();
			 itr != _by_id.end();
			 ++itr)
		{
			free(itr->second.name);
		}
	}
	
	RefID get_id(const string& name,
				 Sequence* seq)
	{
		if (name.empty())
			return 0;
#if ENABLE_THREADS
        table_lock.lock();
#endif
		uint64_t _id = hash_string(name.c_str());
		pair<InvertedIDTable::iterator, bool> ret = 
		_by_id.insert(make_pair(_id, SequenceInfo(_next_id, NULL, NULL)));
		if (ret.second == true)
		{			
			char* _name = NULL;
			if (_keep_names)
				_name = strdup(name.c_str());
			ret.first->second.name = _name;
			ret.first->second.seq	= seq;
			++_next_id;
		}
		assert (_id);
#if ENABLE_THREADS
        table_lock.unlock();
#endif
		return _id;
	}
	
	// You must call invert() before using this function
	const char* get_name(RefID ID) const
	{
		InvertedIDTable::const_iterator itr = _by_id.find(ID);
		if (itr != _by_id.end())
        {
            //const SequenceInfo& info = itr->second;
			return itr->second.name;
        }
		else
        {
			return NULL;
        }
	}
	
	Sequence* get_seq(RefID ID) const
	{
		InvertedIDTable::const_iterator itr = _by_id.find(ID);
		if (itr != _by_id.end())
			return itr->second.seq;
		else
			return NULL;
	}
	
	const SequenceInfo* get_info(RefID ID) const
	{
		
		InvertedIDTable::const_iterator itr = _by_id.find(ID);
		if (itr != _by_id.end())
		{
			return &(itr->second);
		}
		else
			return NULL;
	}
	
	int observation_order(RefID ID) const
	{
		InvertedIDTable::const_iterator itr = _by_id.find(ID);
		if (itr != _by_id.end())
		{
			return itr->second.observation_order;
		}
		else
			return -1;
	}
    
    void order_recs_lexicographically() 
    {
        map<string, RefID> str_to_id;
        
        for (InvertedIDTable::iterator i = _by_id.begin(); i != _by_id.end(); ++i)
        {
            str_to_id[i->second.name] = i->first;
            //fprintf(stderr, "%d: %s\n", i->second.observation_order, i->second.name);
        }
        
        size_t new_order = 1;
        for (map<string, RefID>::iterator i = str_to_id.begin(); i != str_to_id.end(); ++i, ++new_order)
        {
            _by_id.find(get_id(i->first, NULL))->second.observation_order = new_order;
            verbose_msg( "%lu: %s\n", new_order, i->first.c_str());
        }
    }
    
    void print_rec_ordering()
    {
        for (InvertedIDTable::iterator i = _by_id.begin(); i != _by_id.end(); ++i)
        {
            verbose_msg( "%lu: %s\n", i->second.observation_order, i->second.name);
        }
    }
	
	iterator begin() { return _by_id.begin(); }
	iterator end() { return _by_id.end(); }
	
	const_iterator begin() const { return _by_id.begin(); }
	const_iterator end() const { return _by_id.end(); }
	
	size_t size() const { return _by_id.size(); }
	
	void clear()
	{
		//_by_name.clear();
		_by_id.clear();
	}
	
private:
	
	// This is FNV-1, see http://en.wikipedia.org/wiki/Fowler_Noll_Vo_hash
	inline uint64_t hash_string(const char* __s)
	{
		uint64_t hash = 0xcbf29ce484222325ull;
		for ( ; *__s; ++__s)
		{
			hash *= 1099511628211ull;
			hash ^= *__s;
		}
		return hash;
	}
	
	//IDTable _by_name;
	RefID _next_id;
	bool _keep_names;
	InvertedIDTable _by_id;
#if ENABLE_THREADS
    static boost::mutex table_lock;
#endif
};


bool hit_insert_id_lt(const ReadHit& h1, const ReadHit& h2);

/******************************************************************************
 The HitFactory abstract class is responsible for returning a single ReadHit 
 from an alignment file.  The only class that actually implements this interface
 right now in Cufflinks is SAMHitFactory
*******************************************************************************/
class HitFactory
{
public:
    
	HitFactory(ReadTable& insert_table, 
			   RefSequenceTable& reference_table) : 
		_insert_table(insert_table), 
		_ref_table(reference_table),
        _num_seq_header_recs(0) {}
	
	HitFactory& operator=(const HitFactory& rhs) 
	{
		if (this != &rhs)
		{
			//_hit_file = rhs._hit_file;
			_insert_table = rhs._insert_table;
			_ref_table = rhs._ref_table;
		}
		return *this;
	}
	virtual ~HitFactory() {}
	
	ReadHit create_hit(const string& insert_name, 
					   const string& ref_name,
					   int left,
					   const vector<CigarOp>& cigar,
					   CuffStrand source_strand,
					   const string& partner_ref,
					   int partner_pos,
					   unsigned int  edit_dist,
					   int num_hits,
                       float base_mass,
                       uint32_t sam_flag);
	
	ReadHit create_hit(const string& insert_name, 
					   const string& ref_name,
					   uint32_t left,
					   uint32_t read_len,
					   CuffStrand source_strand,
					   const string& partner_ref,
					   int partner_pos,
					   unsigned int  edit_dist,
					   int num_hits,
                       float base_mass,
                       uint32_t sam_flag);
	
	virtual void reset() = 0;
	
	virtual void undo_hit() = 0;
	
	// next_record() should always set _curr_pos before reading the
	// next_record so undo_hit() will work properly.
	virtual bool next_record(const char*& buf, size_t& buf_size) = 0;
	
	virtual bool records_remain() const = 0;
	
	virtual bool get_hit_from_buf(const char* bwt_buf, 
								  ReadHit& bh,
								  bool strip_slash,
								  char* name_out = NULL,
								  char* name_tags = NULL) = 0;
	
	RefSequenceTable& ref_table() { return _ref_table; }
	
	//FILE* hit_file() { return _hit_file; }
	
    virtual bool inspect_header() = 0;
    
    const ReadGroupProperties& read_group_properties()
    {
        return _rg_props;
    }
    
protected:
    
    bool parse_header_string(const string& header_rec,
                             ReadGroupProperties& rg_props);
    
	void parse_header_lines(const string& headers,
							ReadGroupProperties& rg_props);

    void finalize_rg_props();
    
    // TODO: We want to keep a collection of these, indexed by RG ID.  See #180
    ReadGroupProperties _rg_props; 
    
private:

    
	ReadTable& _insert_table;
	RefSequenceTable& _ref_table;
    uint32_t _num_seq_header_recs;
};

/******************************************************************************
 SAMHitFactory turns SAM alignments into ReadHits
*******************************************************************************/
class SAMHitFactory : public HitFactory
{
public:
	SAMHitFactory(const string& hit_file_name, 
				  ReadTable& insert_table, 
				  RefSequenceTable& reference_table) : 
		HitFactory(insert_table, reference_table), 
		_line_num(0), 
		_curr_pos(0) 
	{
		_hit_file = fopen(hit_file_name.c_str(), "r");
		if (_hit_file == NULL)
		{
			throw std::runtime_error("Error: could not open file for reading");
		}
        
        if (inspect_header() == false)
        {
            throw std::runtime_error("Error: could not parse SAM header");
        }
        
        // Override header-inferred read group properities with whatever
        // the user supplied.
        if (global_read_properties != NULL)
        {
            _rg_props = *global_read_properties;
        }
	}
	
	~SAMHitFactory() 
	{
		if (_hit_file)
		{
			fclose(_hit_file);
		}
	}
	
	virtual void undo_hit() 
	{ 
		fseeko(_hit_file, _curr_pos, SEEK_SET); 
		--_line_num;
	}
	
	void reset() { rewind(_hit_file); }
	
	void mark_curr_pos() { _curr_pos = ftell(_hit_file); }
	bool records_remain() const { return !feof(_hit_file); }
	
	bool next_record(const char*& buf, size_t& buf_size);
	
	bool get_hit_from_buf(const char* bwt_buf, 
						  ReadHit& bh,
						  bool strip_slash,
						  char* name_out = NULL,
						  char* name_tags = NULL);
    
    bool inspect_header();
    
private:
	static const size_t _hit_buf_max_sz = 10 * 1024;
	char _hit_buf[_hit_buf_max_sz];
	int _line_num;
	
	FILE* _hit_file;
	off_t _curr_pos;
};

class BAMHitFactory : public HitFactory
{
public:
	BAMHitFactory(ReadTable& insert_table,
				  RefSequenceTable& reference_table) :
	HitFactory(insert_table, reference_table)
	{}

	bool get_hit_from_bam1t(const bam1_t* hit_buf,
							const bam_hdr_t* header,
							ReadHit& bh,
							bool strip_slash,
							char* name_out,
							char* name_tags);

	bool records_remain() const
	{
		return !_eof_encountered;
	}

 protected:
	int64_t _curr_pos;
	int64_t _beginning;
    bool _eof_encountered;
};

#ifndef HAVE_HTSLIB

/******************************************************************************
 SamtoolsHitFactory turns SAM alignments into ReadHits
 *******************************************************************************/
class SamtoolsHitFactory : public BAMHitFactory
{
public:
    SamtoolsHitFactory(const string& hit_file_name,
				  ReadTable& insert_table, 
				  RefSequenceTable& reference_table) : 
	BAMHitFactory(insert_table, reference_table)
	{
		_hit_file = samopen(hit_file_name.c_str(), "rb", 0);
        
        memset(&_next_hit, 0, sizeof(_next_hit));
        
		if (_hit_file == NULL || _hit_file->header == NULL) 
		{
			throw std::runtime_error("Fail to open BAM file");
		}
		
		_beginning = bgzf_tell(_hit_file->x.bam);
        _eof_encountered = false;
        
        if (inspect_header() == false)
        {
            throw std::runtime_error("Error: could not parse BAM header");
        }
        
        // Override header-inferred read group properities with whatever
        // the user supplied.
        if (global_read_properties != NULL)
        {
            _rg_props = *global_read_properties;
        }
	}	
	
	~SamtoolsHitFactory()
	{
		if (_hit_file)
		{
			samclose(_hit_file);
		}
	}
	
	void mark_curr_pos() 
	{ 
		_curr_pos = bgzf_tell(_hit_file->x.bam);
	}

	
	void undo_hit() 
	{ 
		bgzf_seek(_hit_file->x.bam, _curr_pos, SEEK_SET);
		//--_line_num;
	}
    
	void reset() 
	{ 
		if (_hit_file && _hit_file->x.bam)
		{
			bgzf_seek(_hit_file->x.bam, _beginning, SEEK_SET);
            _eof_encountered = false;
		}
	}
	
	
	bool next_record(const char*& buf, size_t& buf_size);
	
	bool get_hit_from_buf(const char* bwt_buf, 
						  ReadHit& bh,
						  bool strip_slash,
						  char* name_out = NULL,
						  char* name_tags = NULL);
    
    bool inspect_header();
	
private:
	samfile_t* _hit_file; 
	bam1_t _next_hit; 
};

#else

/******************************************************************************
 HTSHitFactory uses htslib to turn SAM/BAM/CRAM alignments into ReadHits
 *******************************************************************************/

class HTSHitFactory : public BAMHitFactory
{
public:
    HTSHitFactory(const string& hit_file_name,
				  ReadTable& insert_table,
				  RefSequenceTable& reference_table) :
	BAMHitFactory(insert_table, reference_table)
	{
		_hit_file = hts_open(hit_file_name.c_str(), "r");
		if(!_hit_file)
			throw std::runtime_error("Failed to open SAM/BAM/CRAM file " + hit_file_name);
		_file_header = sam_hdr_read(_hit_file);
		if(!_file_header)
		{
			hts_close(_hit_file);
			throw std::runtime_error("Failed to read header from " + hit_file_name);
		}
		_next_hit = bam_init1();
		if(!_next_hit)
			throw std::runtime_error("Failed to allocate SAM record");
		_beginning = tell();
		// At the time of writing, *only* immediately after reading the header
		// specifically of a SAM file, _hit_file->line contains the first record
		// of the SAM file without its newline terminator:
		if(_hit_file->line.l != 0)
			_beginning -= (_hit_file->line.l + 1);
		_eof_encountered = false;

		if (inspect_header() == false)
        {
            throw std::runtime_error("Error: could not parse BAM header");
        }

        // Override header-inferred read group properities with whatever
        // the user supplied.
        if (global_read_properties != NULL)
        {
            _rg_props = *global_read_properties;
        }
	}

	~HTSHitFactory()
	{
		if(_next_hit)
			bam_destroy1(_next_hit);
		if(_hit_file)
			hts_close(_hit_file);
	}

	int64_t tell()
	{
		if(_hit_file->is_bgzf)
			return bgzf_tell(_hit_file->fp.bgzf);
		else if(_hit_file->is_cram)
			return htell(cram_fd_get_fp(_hit_file->fp.cram));
		else
		{
			assert(_hit_file->line.l == 0);
			return htell(_hit_file->fp.hfile);
		}
	}

	void mark_curr_pos()
	{
		_curr_pos = tell();
	}

	void seek_set(int64_t offset)
	{
		int64_t ret;
		if(_hit_file->is_bgzf)
			ret = bgzf_seek(_hit_file->fp.bgzf, offset, SEEK_SET);
		else if(_hit_file->is_cram)
			ret = hseek(cram_fd_get_fp(_hit_file->fp.cram), offset, SEEK_SET);
		else
			ret = hseek(_hit_file->fp.hfile, offset, SEEK_SET);
		if(ret < 0)
			throw std::runtime_error("htslib: seek failed");
	}

	void undo_hit()
	{
		seek_set(_curr_pos);
	}

	void reset()
	{
		printf("--- RESET ---\n");
		if(_hit_file)
		{
			seek_set(_beginning);
			_eof_encountered = false;
		}
	}

	bool next_record(const char*& buf, size_t& buf_size);

	bool get_hit_from_buf(const char* orig_bwt_buf,
						  ReadHit& bh,
						  bool strip_slash,
						  char* name_out = NULL,
						  char* name_tags = NULL);

	bool inspect_header();

 private:
	htsFile* _hit_file;
	bam_hdr_t* _file_header;
	bam1_t* _next_hit;
};

#endif

class AbundanceGroup;
    
/******************************************************************************
 BAMHitFactory turns SAM alignments into ReadHits
 *******************************************************************************/
class PrecomputedExpressionHitFactory : public HitFactory
{
public:
    PrecomputedExpressionHitFactory(const string& expression_file_name,
                                    ReadTable& insert_table,
                                    RefSequenceTable& reference_table) :
    HitFactory(insert_table, reference_table), _expression_file_name(expression_file_name), _ifs(expression_file_name.c_str()),
    _ia(boost::shared_ptr<boost::archive::binary_iarchive>(new boost::archive::binary_iarchive(_ifs)))
    {
        load_count_tables(expression_file_name);
        
        if (inspect_header() == false)
        {
            throw std::runtime_error("Error: could not parse CXB header");
        }
        
        // Override header-inferred read group properities with whatever
        // the user supplied.
        if (global_read_properties != NULL)
        {
            _rg_props = *global_read_properties;
        }
        
        load_checked_parameters(expression_file_name);
        
        //map<string, AbundanceGroup> single_sample_tracking;
        
        _num_loci = 0;
        *_ia >> _num_loci;
        
        _curr_locus_idx = 0;
        _last_locus_id = -1;
    }
    
    ~PrecomputedExpressionHitFactory()
    {
        
    }
    
    void mark_curr_pos()
    {
        
    }
    
    void undo_hit()
    {
    }
    
    bool records_remain() const
    {
        return false;
    }
    
    void reset()
    {
        _ifs.clear() ;
        _ifs.seekg(0, ios::beg);
        _ia = boost::shared_ptr<boost::archive::binary_iarchive>(new boost::archive::binary_iarchive(_ifs));
        size_t num_loci = 0;
        *_ia >> num_loci;
        _last_locus_id = -1;
        _curr_locus_idx = 0;
        _curr_ab_groups.clear();
    }
    
    bool next_record(const char*& buf, size_t& buf_size);
    
    bool get_hit_from_buf(const char* bwt_buf, 
                          ReadHit& bh,
                          bool strip_slash,
                          char* name_out = NULL,
                          char* name_tags = NULL);
    
    bool inspect_header();
    
    boost::shared_ptr<const AbundanceGroup> next_locus(int locus_id, bool cache_locus);
    
    boost::shared_ptr<const AbundanceGroup> get_abundance_for_locus(int locus_id);
    void clear_abundance_for_locus(int locus_id);
    
    double get_compat_mass(int locus_id)
    {
       map<int, double >::iterator i = compat_mass.find(locus_id);
       if (i != compat_mass.end())
       {
           return i->second;
       }
       else
       {
           return 0;
       }
    }

    double get_total_mass(int locus_id)
    {
        map<int, double >::iterator i = total_mass.find(locus_id);
        if (i != total_mass.end())
        {
            return i->second;
        }
        else
        {
            return 0;
        }
    }

    
private:
    
    void load_count_tables(const string& expression_file_name);
    void load_checked_parameters(const string& expression_file_name);
    
    //map<int, boost::shared_ptr<const AbundanceGroup> > ab_group_table;
    size_t _num_loci;
    size_t _curr_locus_idx;
    int _last_locus_id;
    std::ifstream _ifs;
    string _expression_file_name;
    boost::shared_ptr<boost::archive::binary_iarchive> _ia;
    map<int, double> compat_mass;
    map<int, double> total_mass;
    map<int, boost::shared_ptr<const AbundanceGroup> > _curr_ab_groups;
    
    boost::shared_ptr<ReadGroupProperties> _cached_rg_props;
    
#if ENABLE_THREADS
    boost::mutex _factory_lock;
#endif
};
    
    
// Forward declaration of BundleFactory, because MateHit will need a pointer
// back to the Factory that created.  Ultimately, we should replace this
// with a pointer back to the ReadGroupProperty object corresponding to each 
// MateHit.  That, however, requires that we link fragment length distributions
// and bias models, etc, with each read group, and that's more than we can 
// afford to implement right now.

/*******************************************************************************
 MateHit is a class that encapsulates a paired-end alignment as a single object.
 MateHits can be "open" when one hit has been read from a stream of individual
 read alignments, but the other hasn't.  A "closed" MateHit is one where either
 both read alignments have been installed in the MateHit, or one read hit has,
 but the other will never come (i.e. singletons)
*******************************************************************************/
class MateHit
{
public:
    MateHit() : 
    _refid(0), 
    _left_alignment(NULL),
    _right_alignment(NULL),
    _collapse_mass(0.0),
    _is_mapped(false){}
    
	MateHit(boost::shared_ptr<ReadGroupProperties const> rg_props,
            RefID refid, 
			const ReadHit* left_alignment, 
			const ReadHit* right_alignment) : 
    _rg_props(rg_props),
	_refid(refid), 
	_left_alignment(left_alignment),
	_right_alignment(right_alignment),
	_collapse_mass(0.0),
	_is_mapped(false)
	{
		//_expected_inner_dist = min(genomic_inner_dist(), _expected_inner_dist);
	}
	~MateHit()
	{
		//fprintf(stderr, "Killing hit %lx\n",this);
	}

	//bool closed() {return _closed;}
	
    boost::shared_ptr<ReadGroupProperties const> read_group_props() const { return _rg_props; }
    
	const ReadHit* left_alignment() const {return _left_alignment;}
	void left_alignment(const ReadHit* left_alignment) 
	{
		_left_alignment = left_alignment;
	}
	
	const ReadHit* right_alignment() const {return _right_alignment;}					
	void right_alignment(const ReadHit* right_alignment)  
	{
		_right_alignment = right_alignment;
	}
	
	bool is_mapped() const {return _is_mapped;}					
	void is_mapped(bool mapped) 
	{ 
		_is_mapped = mapped; 
	}
	
	int num_hits() const 
	{
		assert(_left_alignment);
		return _left_alignment->num_hits();
	}
	
	bool is_multi() const 
	{
		return num_hits() > 1;
	}
		
	bool is_pair() const
	{
		return (_left_alignment && _right_alignment);
	}

	int left() const 
	{
		if (_right_alignment && _left_alignment)
		{
			return min(_right_alignment->left(),_left_alignment->left());
		}
		if (_left_alignment)
			return _left_alignment->left();
		else if (_right_alignment)
			return _right_alignment->left(); 
		return -1;
	}
	
	int right() const 
	{
		if (_right_alignment && _left_alignment)
		{
			return max(_right_alignment->right(),_left_alignment->right());
		}
		if (_right_alignment)
			return _right_alignment->right();
		else if (_left_alignment)
			return _left_alignment->right(); 
		return -1;
	}
	
	CuffStrand strand() const 
	{
		CuffStrand left_strand = CUFF_STRAND_UNKNOWN;
		CuffStrand right_strand = CUFF_STRAND_UNKNOWN;
		if (_left_alignment)
		{
			left_strand = _left_alignment->source_strand();
		}
		if (_right_alignment)
		{
			right_strand = _right_alignment->source_strand();
			//assert ( s != CUFF_STRAND_UNKNOWN ? s == r : true);
		}
		assert (left_strand == right_strand || 
				left_strand == CUFF_STRAND_UNKNOWN || 
				right_strand == CUFF_STRAND_UNKNOWN);
		
		return max(left_strand, right_strand);
	}
	
	
	bool contains_splice() const
	{
		if (_right_alignment)
			return (_left_alignment->contains_splice() || _right_alignment->contains_splice());
		return (_left_alignment->contains_splice());
	}
	
	InsertID insert_id() const
	{
		if (_left_alignment) return _left_alignment->insert_id();
		if (_right_alignment) return _right_alignment->insert_id();
		return 0;
	}
	
	RefID ref_id() const { return _refid; }
	
	int genomic_inner_dist() const 
	{
		if (_left_alignment && _right_alignment)
		{
			return _right_alignment->left() - _left_alignment->right();
		}
		else
		{
			return -1;
		}
		return -1;
	}
	
	pair<int,int> genomic_outer_span() const
	{
		if (_left_alignment && _right_alignment)
		{
			return make_pair(left(),
							 right() - 1);
		}

		return make_pair(-1,-1);
	}	
	
	pair<int,int> genomic_inner_span() const 
	{
		if (_left_alignment && _right_alignment)
		{
			return make_pair(_left_alignment->right(),
							 _right_alignment->left() - 1);
		}

		return make_pair(-1,-1);
	}
	
	// MRT is incorrect and not added to rg_props until after inspect_map
    // We are ignoring the mass reported by the ReadHits and re-calculating based on multi-hits
	double mass() const
	{
        double base_mass = 1.0;

        if (is_multi())
		{
			boost::shared_ptr<MultiReadTable> mrt = _rg_props->multi_read_table();
			if (mrt)
				return mrt->get_mass(*this);
			else
				return base_mass/num_hits();
		}
		return base_mass;
	}
    
	double internal_scale_mass() const
	{
       	double m = mass();
        m *= 1.0 / _rg_props->internal_scale_factor();
        
        return m;
	}
	
	unsigned int  edit_dist() const
	{
		unsigned int edits = 0;
		if (_left_alignment)
			edits += _left_alignment->edit_dist();
		if (_right_alignment)
			edits += _right_alignment->edit_dist();
		return edits;
	}
	
	double collapse_mass() const { return _collapse_mass; }
	void collapse_mass(double m) { _collapse_mass = m; }
	void incr_collapse_mass(double incr) { _collapse_mass += incr; }
	
private:
	
    boost::shared_ptr<ReadGroupProperties const> _rg_props;
	RefID _refid;
	const ReadHit* _left_alignment;
	const ReadHit* _right_alignment;
	double _collapse_mass;
	bool _is_mapped;
	//bool _closed;
};

bool mate_hit_lt(const MateHit& lhs, const MateHit& rhs);

bool hits_eq_mod_id(const ReadHit& lhs, const ReadHit& rhs);

bool hits_eq_non_multi(const MateHit& lhs, const MateHit& rhs);
bool hits_eq_non_multi_non_replicate(const MateHit& lhs, const MateHit& rhs);

bool hits_equals(const MateHit& lhs, const MateHit& rhs);

bool has_no_collapse_mass(const MateHit& hit);

// Assumes hits are sorted by mate_hit_lt
void collapse_hits(const vector<MateHit>& hits,
				   vector<MateHit>& non_redundant);

void normalize_counts(std::vector<boost::shared_ptr<ReadGroupProperties> > & all_read_groups);

HitFactory* createSamHitFactory(const string& hit_file_name, ReadTable& it, RefSequenceTable& rt);

#endif
