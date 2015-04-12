
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <stdint.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <iterator>
#include <algorithm>

#include "SPCFile.h"
#include "cpath.h"

#ifdef WIN32
#define strcasecmp _stricmp
#endif

#define SPC_SIGNATURE_HEAD      "SNES-SPC700 Sound File Data"
#define SPC_SIGNATURE           "SNES-SPC700 Sound File Data v0.30"
#define SPC_HEADER_SIZE         0x100
#define SPC_MIN_SIZE            0x10200

#define XID6_TICK_UNIT          64000

#define ALIGN32(x)  (((x) + 3) & ~3)

SPCFile::SPCFile()
{
	memset(ram, 0xff, 0x10000);
	memset(dsp, 0xff, 0x80);
	memset(extra_ram, 0xff, 0x40);
}

SPCFile::~SPCFile()
{
}

bool SPCFile::IsSPCFile(const std::string& filename)
{
	FILE *fp = NULL;
	uint8_t header[SPC_HEADER_SIZE];

	off_t off_spc_size = path_getfilesize(filename.c_str());
	if (off_spc_size == -1 || off_spc_size < SPC_MIN_SIZE) {
		return false;
	}

	fp = fopen(filename.c_str(), "rb");
	if (fp == NULL) {
		return false;
	}

	if (fread(header, 1, SPC_HEADER_SIZE, fp) != SPC_HEADER_SIZE) {
		fclose(fp);
		return false;
	}

	if (memcmp(header, SPC_SIGNATURE_HEAD, strlen(SPC_SIGNATURE_HEAD)) != 0 ||
		header[0x21] != 0x1a || header[0x22] != 0x1a) {
		fclose(fp);
		return false;
	}

	fclose(fp);
	return true;
}

SPCFile * SPCFile::Load(const std::string& filename)
{
	off_t off_spc_size = path_getfilesize(filename.c_str());
	if (off_spc_size == -1 || off_spc_size < SPC_MIN_SIZE) {
		return NULL;
	}
	size_t spc_size = (size_t) off_spc_size;

	FILE * fp = fopen(filename.c_str(), "rb");
	if (fp == NULL) {
		return NULL;
	}

	// read header
	uint8_t header[SPC_HEADER_SIZE];
	if (fread(header, 1, SPC_HEADER_SIZE, fp) != SPC_HEADER_SIZE) {
		fclose(fp);
		return NULL;
	}
	rewind(fp);

	// signature
	if (memcmp(header, SPC_SIGNATURE_HEAD, strlen(SPC_SIGNATURE_HEAD)) != 0 ||
		header[0x21] != 0x1a || header[0x22] != 0x1a) {
		fclose(fp);
		return NULL;
	}

	// read whole file
	uint8_t * data = new uint8_t[spc_size];
	if (fread(data, 1, spc_size, fp) != spc_size) {
		delete[] data;
		fclose(fp);
		return NULL;
	}

	// create new SPC object
	SPCFile * spc = new SPCFile();

	// SPC700 registers
	spc->regs.pc = header[0x25] | (header[0x26] << 8);
	spc->regs.a = header[0x27];
	spc->regs.x = header[0x28];
	spc->regs.y = header[0x29];
	spc->regs.psw = header[0x2a];
	spc->regs.sp = header[0x2b];

	// RAM
	memcpy(spc->ram, &data[0x100], 0x10000);
	
	// DSP registers
	memcpy(spc->dsp, &data[0x10100], 0x80);

	// Extra RAM
	memcpy(spc->extra_ram, &data[0x101c0], 0x40);

	// Parse [ID666](http://vspcplay.raphnet.net/spc_file_format.txt) if available.
	spc->tags.clear();
	if (header[0x23] == 0x1a) {
		char s[256];
		uint32_t u;

		memcpy(s, &header[0x2e], 32);
		s[32] = '\0';
		spc->SetStringTag(XID6_SONG_NAME, s);

		memcpy(s, &header[0x4e], 32);
		s[32] = '\0';
		spc->SetStringTag(XID6_GAME_NAME, s);

		memcpy(s, &header[0x6e], 16);
		s[16] = '\0';
		spc->SetStringTag(XID6_DUMPER_NAME, s);

		memcpy(s, &header[0x7e], 32);
		s[32] = '\0';
		spc->SetStringTag(XID6_COMMENT, s);

		if (header[0xd2] < 0x30) {
			// binary format
			u = header[0x9e] | (header[0x9f] << 8) | (header[0xa0] << 16) | (header[0xa1] << 24);
			if (u != 0) {
				spc->SetIntegerTag(XID6_DUMPED_DATE, u, 4);
			}

			u = header[0xa9] | (header[0xaa] << 8) | (header[0xab] << 16);
			if (u != 0) {
				spc->SetIntegerTag(XID6_INTRO_LENGTH, XID6TicksToMilliSeconds(u) / 1000, 4);
			}

			u = header[0xac] | (header[0xad] << 8) | (header[0xae] << 16) | (header[0xaf] << 24);
			if (u != 0) {
				spc->SetIntegerTag(XID6_FADE_LENGTH, XID6TicksToMilliSeconds(u), 4);
			}

			memcpy(s, &header[0xb0], 32);
			s[32] = '\0';
			spc->SetStringTag(XID6_ARTIST_NAME, s);

			// [0xd0] Default channel disables (0 = enable, 1 = disable)

			spc->SetIntegerTag(XID6_EMULATOR, header[0xd1], 1);
		}
		else {
			// text format
			memcpy(s, &header[0x9e], 11);
			s[11] = '\0';
			if (strcmp(s, "") != 0) {
				int year;
				int month;
				int day;

				// MM/DD/YYYY is expected (according to SPC File Format v0.30)
				if (ParseDateString(s, year, month, day)) {
					spc->SetIntegerTag(XID6_DUMPED_DATE, year * 10000 + month * 100 + day, 4);
				}
			}

			memcpy(s, &header[0xa9], 3);
			s[3] = '\0';
			if (strcmp(s, "") != 0) {
				u = strtoul(s, NULL, 10);
				spc->SetIntegerTag(XID6_INTRO_LENGTH, XID6TicksToMilliSeconds(u) / 1000, 4);
			}

			memcpy(s, &header[0xac], 5);
			s[5] = '\0';
			if (strcmp(s, "") != 0) {
				u = strtoul(s, NULL, 10);
				if (u != 0) {
					spc->SetIntegerTag(XID6_FADE_LENGTH, u * XID6TicksToMilliSeconds(u), 4);
				}
			}

			memcpy(s, &header[0xb1], 32);
			s[32] = '\0';
			spc->SetStringTag(XID6_ARTIST_NAME, s);

			// [0xd1] Default channel disables (0 = enable, 1 = disable)

			memcpy(s, &header[0xd2], 1);
			s[1] = '\0';
			if (strcmp(s, "") != 0) {
				u = strtoul(s, NULL, 10);
				spc->SetIntegerTag(XID6_EMULATOR, u, 1);
			}
		}
	}

	// Parse Extended ID666 if available
	const size_t xid6_start_offset = 0x10200;
	size_t xid6_offset = xid6_start_offset;
	if (spc_size > xid6_offset + 8 && memcmp(&data[xid6_offset], "xid6", 4) == 0) {
		// get xid6 chunk size
		uint32_t xid6_whole_size = data[xid6_offset + 4] | (data[xid6_offset + 5] << 8) | (data[xid6_offset + 6] << 16) | (data[xid6_offset + 7] << 24);

		// skip the xid6 header
		xid6_offset += 8;

		// determine the end offset
		if (xid6_offset + xid6_whole_size > spc_size) {
			xid6_whole_size = (uint32_t)(spc_size - xid6_offset);
		}
		const size_t xid6_end_offset = xid6_offset + xid6_whole_size;

		// read each sub-chunks
		while (xid6_offset + 4 <= xid6_end_offset) {
			XID6ItemId xid6_id = (XID6ItemId)data[xid6_offset];
			XID6TypeId xid6_type = (XID6TypeId)data[xid6_offset + 1];
			uint16_t xid6_length = data[xid6_offset + 2] | (data[xid6_offset + 3] << 8);

			if (xid6_type == XID6_TYPE_LENGTH) {
				spc->SetTagValue(xid6_id, xid6_type, &data[xid6_offset + 2], 2);
				xid6_offset += 4;
			}
			else {
				xid6_offset += 4;

				if (xid6_offset + xid6_length <= xid6_end_offset) {
					spc->SetTagValue(xid6_id, xid6_type, &data[xid6_offset], xid6_length);
				}

				xid6_offset += ALIGN32(xid6_length);
			}
		}
	}

	delete[] data;
	fclose(fp);

	return spc;
}

bool SPCFile::Save(const std::string& filename) const
{
	FILE * spc_file = fopen(filename.c_str(), "wb");
	if (spc_file == NULL) {
		return false;
	}

	uint8_t header[0x100];
	memset(header, 0, 0x100);

	uint8_t reserved[0x40];
	memset(reserved, 0, 0x40);

	// signature and version
	memcpy(header, SPC_SIGNATURE, strlen(SPC_SIGNATURE));
	header[0x21] = 0x1a;
	header[0x22] = 0x1a;
	header[0x24] = 30;

	// SPC700 registers
	header[0x25] = regs.pc & 0xff;
	header[0x26] = (regs.pc >> 8) & 0xff;
	header[0x27] = regs.a;
	header[0x28] = regs.x;
	header[0x29] = regs.y;
	header[0x2a] = regs.psw;
	header[0x2b] = regs.sp;

	// write ID666 tags (text format)
	if (tags.size() == 0) {
		header[0x23] = 0x1b;
	}
	else {
		char s[32 + 1];

		header[0x23] = 0x1a;

		strncpy(s, GetStringTag(XID6_SONG_NAME).c_str(), 32);
		s[32] = '\0';
		memcpy(&header[0x2e], s, 32);

		strncpy(s, GetStringTag(XID6_GAME_NAME).c_str(), 32);
		s[32] = '\0';
		memcpy(&header[0x4e], s, 32);

		strncpy(s, GetStringTag(XID6_DUMPER_NAME).c_str(), 16);
		s[16] = '\0';
		memcpy(&header[0x6e], s, 16);

		strncpy(s, GetStringTag(XID6_COMMENT).c_str(), 32);
		s[32] = '\0';
		memcpy(&header[0x7e], s, 32);

		if (tags.count(XID6_DUMPED_DATE) != 0) {
			uint32_t yyyymmdd = GetIntegerTag(XID6_DUMPED_DATE);
			uint32_t year = yyyymmdd / 10000;
			uint32_t month = (yyyymmdd / 100) % 100;
			uint32_t day = yyyymmdd % 100;

			sprintf(s, "%02d/%02d/%d", month, day, year);
			memcpy(&header[0x9e], s, 11);
		}

		uint32_t length_in_ticks = GetPlaybackLength();
		if (length_in_ticks != 0)
		{
			uint32_t seconds = XID6TicksToMilliSeconds(length_in_ticks) / 1000;
			sprintf(s, "%d", std::min<uint32_t>(seconds, 999));
			memcpy(&header[0xa9], s, 3);
		}

		if (tags.count(XID6_FADE_LENGTH) != 0) {
			uint32_t ticks = GetIntegerTag(XID6_FADE_LENGTH);
			uint32_t milliseconds = XID6TicksToMilliSeconds(ticks);

			sprintf(s, "%d", std::min<uint32_t>(milliseconds, 99999));
			memcpy(&header[0xac], s, 5);
		}

		strncpy(s, GetStringTag(XID6_ARTIST_NAME).c_str(), 32);
		s[32] = '\0';
		memcpy(&header[0xb1], s, 32);

		if (tags.count(XID6_EMULATOR) != 0) {
			uint16_t emuid = GetIntegerTag(XID6_EMULATOR);

			sprintf(s, "%d", emuid);
			memcpy(&header[0xd2], s, 1);
		}
		else {
			// unknown (indicates text format)
			header[0xd2] = '0';
		}
	}

	// write file header
	if (fwrite(header, 1, 0x100, spc_file) != 0x100) {
		fclose(spc_file);
		return false;
	}

	// write RAM
	if (fwrite(ram, 1, 0x10000, spc_file) != 0x10000) {
		fclose(spc_file);
		return false;
	}

	// write DSP registers
	if (fwrite(dsp, 1, 0x80, spc_file) != 0x80) {
		fclose(spc_file);
		return false;
	}

	// write reserved area
	if (fwrite(reserved, 1, 0x40, spc_file) != 0x40) {
		fclose(spc_file);
		return false;
	}

	// write extra RAM
	if (fwrite(extra_ram, 1, 0x40, spc_file) != 0x40) {
		fclose(spc_file);
		return false;
	}

	// determine if Extended ID666 is required
	bool xid6_required = false;
	for (auto itr = tags.begin(); itr != tags.end(); ++itr) {
		const XID6ItemId id = (*itr).first;

		if (DoesTagRequireXID6(id)) {
			xid6_required = true;
			break;
		}
	}

	// write Extended ID666
	if (xid6_required) {
		std::vector<uint8_t> xid6 = GetXID6Block();

		if (fwrite(&xid6[0], 1, xid6.size(), spc_file) != xid6.size()) {
			fclose(spc_file);
			return false;
		}
	}

	fclose(spc_file);
	return true;
}

std::vector<uint8_t> SPCFile::GetXID6Block() const
{
	std::vector<uint8_t> xid6;

	// signature
	xid6.push_back('x');
	xid6.push_back('i');
	xid6.push_back('d');
	xid6.push_back('6');

	// size (will be determined later)
	xid6.push_back(0);
	xid6.push_back(0);
	xid6.push_back(0);
	xid6.push_back(0);

	for (auto itr = tags.begin(); itr != tags.end(); ++itr) {
		const XID6ItemId id = (*itr).first;
		const XID6TagItem & tag = (*itr).second;

		bool required = DoesTagRequireXID6(id);
		if (!required &&
			id != XID6_DUMPED_DATE &&
			id != XID6_INTRO_LENGTH &&
			id != XID6_FADE_LENGTH) {
			continue;
		}

		// Note: all data is 32-bit aligned
		switch (tag.type) {
		case XID6_TYPE_LENGTH:
		{
			uint16_t value = 0;
			for (size_t i = 0; i < std::min<size_t>(tag.value.size(), 2); i++) {
				value |= (uint8_t)tag.value[i] << (8 * i);
			}

			xid6.push_back(id);
			xid6.push_back(tag.type);
			xid6.push_back(value & 0xff);
			xid6.push_back((value >> 8) & 0xff);
			break;
		}

		case XID6_TYPE_INTEGER:
		{
			xid6.push_back(id);
			xid6.push_back(tag.type);
			xid6.push_back(4);
			xid6.push_back(0);

			uint32_t value = 0;
			for (size_t i = 0; i < std::min<size_t>(tag.value.size(), 4); i++) {
				value |= (uint8_t)tag.value[i] << (8 * i);
			}

			xid6.push_back(value & 0xff);
			xid6.push_back((value >> 8) & 0xff);
			xid6.push_back((value >> 16) & 0xff);
			xid6.push_back((value >> 24) & 0xff);
			break;
		}

		case XID6_TYPE_STRING:
		{
			size_t size = strlen(&tag.value[0]) + 1;

			xid6.push_back(id);
			xid6.push_back(tag.type);
			xid6.push_back(size & 0xff);
			xid6.push_back((size >> 8) & 0xff);

			for (size_t i = 0; i < size; i++) {
				xid6.push_back(tag.value[i]);
			}

			size_t aligned_size = ALIGN32(size);
			while (size < aligned_size) {
				xid6.push_back(0);
				size++;
			}
			break;
		}
		}
	}

	size_t whole_size = xid6.size() - 8;
	xid6[4] = whole_size & 0xff;
	xid6[5] = (whole_size >> 8) & 0xff;
	xid6[6] = (whole_size >> 16) & 0xff;
	xid6[7] = (whole_size >> 24) & 0xff;
	return xid6;
}

bool SPCFile::DoesTagRequireXID6(XID6ItemId id) const
{
	bool required = true;

	switch (id) {
	case XID6_SONG_NAME:
	case XID6_GAME_NAME:
	case XID6_COMMENT:
	case XID6_ARTIST_NAME:
		if (GetStringTag(id).size() <= 32) {
			required = false;
		}
		break;

	case XID6_DUMPER_NAME:
		if (GetStringTag(id).size() <= 16) {
			required = false;
		}
		break;

	case XID6_INTRO_LENGTH:
	{
		uint32_t ticks = GetIntegerTag(id);
		uint32_t msecs = XID6TicksToMilliSeconds(ticks);

		if (ticks % XID6_TICK_UNIT == 0 && msecs <= 999999) {
			required = false;
		}
		break;
	}

	case XID6_FADE_LENGTH:
	{
		uint32_t ticks = GetIntegerTag(id);
		uint32_t msecs = XID6TicksToMilliSeconds(ticks);

		if (ticks % (XID6_TICK_UNIT / 1000) == 0 && msecs <= 99999) {
			required = false;
		}
		break;
	}

	case XID6_DUMPED_DATE:
	case XID6_EMULATOR:
		required = false;
		break;

	default:
		break;
	}

	return required;
}

int SPCFile::GetIntegerTag(XID6ItemId id) const
{
	if (tags.count(id) != 0) {
		size_t size = std::min<size_t>(tags.at(id).value.size(), 4);

		uint32_t value = 0;
		for (size_t i = 0; i < size; i++) {
			value |= (uint8_t)tags.at(id).value[i] << (8 * i);
		}

		return value;
	}
	else {
		return 0;
	}
}

std::string SPCFile::GetStringTag(XID6ItemId id) const
{
	if (tags.count(id) != 0) {
		return &tags.at(id).value[0];
	}
	else {
		return "";
	}
}

void SPCFile::SetLengthTag(XID6ItemId id, uint16_t value)
{
	SetIntegerTag(id, value, 2);
	tags[id].type = XID6_TYPE_LENGTH;
}

void SPCFile::SetIntegerTag(XID6ItemId id, uint32_t value, size_t size)
{
	std::vector<char> data;

	size = std::min<size_t>(size, 4);
	for (size_t i = 0; i < size; i++) {
		data.push_back((value >> (8 * i)) & 0xff);
	}

	XID6TagItem tag;
	tag.type = XID6_TYPE_INTEGER;
	tag.value = data;
	tags[id] = tag;
}

void SPCFile::SetStringTag(XID6ItemId id, const std::string & str)
{
	if (str.empty()) {
		tags.erase(id);
	}
	else {
		std::vector<char> data;
		data.reserve(str.size() + 1);
		for (size_t i = 0; i <= str.size(); i++) {
			data.push_back(str[i]);
		}

		XID6TagItem tag;
		tag.type = XID6_TYPE_STRING;
		tag.value = data;
		tags[id] = tag;
	}
}

uint32_t SPCFile::GetPlaybackLength() const
{
	uint32_t ticks = 0;

	if (tags.count(XID6_INTRO_LENGTH) != 0) {
		ticks += GetIntegerTag(XID6_INTRO_LENGTH);
	}

	if (tags.count(XID6_LOOP_LENGTH) != 0) {
		uint8_t loopcount = 1;
		if (tags.count(XID6_LOOP_COUNT) != 0) {
			loopcount = GetIntegerTag(XID6_LOOP_COUNT);
		}

		ticks += GetIntegerTag(XID6_LOOP_LENGTH) * loopcount;
	}

	if (tags.count(XID6_END_LENGTH) != 0) {
		ticks += GetIntegerTag(XID6_END_LENGTH);
	}

	return ticks;
}

uint32_t SPCFile::XID6TicksToMilliSeconds(uint32_t ticks)
{
	return ticks / (XID6_TICK_UNIT / 1000);
}

uint32_t SPCFile::MilliSecondsToXID6Ticks(uint32_t msecs)
{
	return msecs * (XID6_TICK_UNIT / 1000);
}

std::string SPCFile::XID6TicksToTimeString(uint32_t ticks, bool padding)
{
	double tv = (double)ticks / XID6_TICK_UNIT;

	double seconds = fmod(tv, 60.0);
	unsigned int minutes = (unsigned int)(tv - seconds) / 60;
	unsigned int hours = minutes / 60;
	minutes %= 60;
	hours %= 60;

	char str[64];
	if (hours != 0) {
		sprintf(str, "%u:%02u:%06.3f", hours, minutes, seconds);
	}
	else {
		if (padding || minutes != 0) {
			sprintf(str, "%u:%06.3f", minutes, seconds);
		}
		else {
			sprintf(str, "%.3f", seconds);
		}
	}

	if (!padding) {
		size_t len = strlen(str);
		for (size_t i = len - 1; i >= 0; i--) {
			if (str[i] == '0') {
				str[i] = '\0';
			}
			else {
				if (str[i] == '.') {
					str[i] = '\0';
				}
				break;
			}
		}
	}

	return str;
}

uint32_t SPCFile::TimeStringToXID6Ticks(const std::string & str, bool * p_valid_format)
{
	bool dummy_ret;
	if (p_valid_format == NULL) {
		p_valid_format = &dummy_ret;
	}

	if (str.empty()) {
		*p_valid_format = true;
		return 0;
	}

	// split by colons
	std::vector<std::string> tokens;
	size_t current = 0;
	size_t found;
	while ((found = str.find_first_of(':', current)) != std::string::npos) {
		tokens.push_back(std::string(str, current, found - current));
		current = found + 1;
	}
	tokens.push_back(std::string(str, current, str.size() - current));

	// too many colons?
	if (tokens.size() > 3) {
		*p_valid_format = false;
		return 0;
	}

	// set token for each fields
	const char * s_hours = "0";
	const char * s_minutes = "0";
	const char * s_seconds = "0";
	switch (tokens.size())
	{
	case 1:
		s_seconds = tokens[0].c_str();
		break;

	case 2:
		s_minutes = tokens[0].c_str();
		s_seconds = tokens[1].c_str();
		break;

	case 3:
		s_hours = tokens[0].c_str();
		s_minutes = tokens[1].c_str();
		s_seconds = tokens[2].c_str();
		break;
	}

	if (*s_hours == '\0' || *s_minutes == '\0' || *s_seconds == '\0' ||
		*s_hours == '+' || *s_minutes == '+' || *s_seconds == '+') {
		*p_valid_format = false;
		return 0;
	}

	char * endptr = NULL;
	double n_seconds = strtod(s_seconds, &endptr);
	if (*endptr != '\0' || errno == ERANGE || n_seconds < 0) {
		*p_valid_format = false;
		return 0;
	}

	long n_minutes = strtol(s_minutes, &endptr, 10);
	if (*endptr != '\0' || errno == ERANGE || n_minutes < 0) {
		*p_valid_format = false;
		return 0;
	}

	long n_hours = strtol(s_hours, &endptr, 10);
	if (*endptr != '\0' || errno == ERANGE || n_hours < 0) {
		*p_valid_format = false;
		return 0;
	}

	double tv = n_hours * 3600 + n_minutes * 60 + n_seconds;
	if (tv < 0) {
		*p_valid_format = false;
		return 0;
	}

	*p_valid_format = true;
	return (uint32_t)(tv * XID6_TICK_UNIT + 0.5);
}

std::string SPCFile::ID666IdToEmulatorName(SPCFile::ID666EmulatorId id)
{
	switch (id) {
	case ID666_EMU_ZSNES:
		return "ZSNES";

	case ID666_EMU_SNES9X:
		return "Snes9x";

	case ID666_EMU_ZST2SPC:
		return "ZST2SPC";

	case ID666_EMU_SNESHOUT:
		return "SNEShout";

	case ID666_EMU_ZSNES_W:
		return "ZSNESW";

	case ID666_EMU_SNESGT:
		return "SNESGT";

	default:
		return "";
	}
}

SPCFile::ID666EmulatorId SPCFile::EmulatorNameToID666Id(const char * name)
{
	if (strcasecmp(name, "ZSNES") == 0) {
		return ID666_EMU_ZSNES;
	}
	else if (strcasecmp(name, "Snes9x") == 0) {
		return ID666_EMU_SNES9X;
	}
	else if (strcasecmp(name, "ZST2SPC") == 0) {
		return ID666_EMU_ZST2SPC;
	}
	else if (strcasecmp(name, "SNEShout") == 0) {
		return ID666_EMU_SNESHOUT;
	}
	else if (strcasecmp(name, "ZSNES/W") == 0) {
		return ID666_EMU_ZSNES_W;
	}
	else if (strcasecmp(name, "SNESGT") == 0) {
		return ID666_EMU_SNESGT;
	}
	else {
		return ID666_EMU_UNKNOWN;
	}
}

bool SPCFile::ImportPSFTag(const std::map<std::string, std::string> & psf_tags)
{
	char * endptr = NULL;

	for (auto itr = psf_tags.begin(); itr != psf_tags.end(); ++itr) {
		const std::string & name = (*itr).first;
		const std::string & value = (*itr).second;

		if (name == "title") {
			SetStringTag(XID6_SONG_NAME, value);
		}
		else if (name == "artist") {
			SetStringTag(XID6_ARTIST_NAME, value);
		}
		else if (name == "game") {
			SetStringTag(XID6_GAME_NAME, value);
		}
		else if (name == "year") {
			if (value.empty()) {
				tags.erase(XID6_COPYRIGHT_YEAR);
			}
			else {
				long num = strtol(value.c_str(), NULL, 10);
				SetLengthTag(XID6_COPYRIGHT_YEAR, (uint16_t)num);
			}
		}
		else if (name == "comment") {
			SetStringTag(XID6_COMMENT, value);
		}
		else if (name == "copyright") {
			SetStringTag(XID6_PUBLISHER_NAME, value);
		}
		else if (name == "snsfby") {
			SetStringTag(XID6_DUMPER_NAME, value);
		}
		else if (name == "volume") {
			if (value.empty()) {
				tags.erase(XID6_VOLUME);
			}
			else {
				double num = strtod(value.c_str(), NULL);
				uint32_t volume = (uint32_t)(num * 65536);
				SetIntegerTag(XID6_VOLUME, volume, 4);
			}
		}
		else if (name == "length") {
			tags.erase(XID6_INTRO_LENGTH);
			tags.erase(XID6_LOOP_LENGTH);
			tags.erase(XID6_LOOP_COUNT);
			tags.erase(XID6_END_LENGTH);

			if (!value.empty()) {
				bool valid_format;
				uint32_t ticks = TimeStringToXID6Ticks(value, &valid_format);
				if (valid_format) {
					SetIntegerTag(XID6_INTRO_LENGTH, ticks, 4);
				}
			}
		}
		else if (name == "fade") {
			if (value.empty()) {
				tags.erase(XID6_FADE_LENGTH);
			}
			else {
				bool valid_format;

				uint32_t ticks = TimeStringToXID6Ticks(value, &valid_format);
				if (valid_format) {
					SetIntegerTag(XID6_FADE_LENGTH, ticks, 4);
				}
			}
		}
		// unofficial tags
		else if (name == "created_at") {
			if (value.empty()) {
				tags.erase(XID6_DUMPED_DATE);
			}
			else {
				int year;
				int month;
				int day;

				if (ParseDateString(value, year, month, day)) {
					SetIntegerTag(XID6_DUMPED_DATE, year * 10000 + month * 100 + day, 4);
				}
			}
		}
		else if (name == "emulator") {
			if (value.empty()) {
				tags.erase(XID6_EMULATOR);
			}
			else {
				long num = strtol(value.c_str(), NULL, 10);
				SetLengthTag(XID6_EMULATOR, (uint16_t)num);
			}
		}
		else if (name == "soundtrack") {
			SetStringTag(XID6_OST_TITLE, value);
		}
		else if (name == "disc") {
			if (value.empty()) {
				tags.erase(XID6_OST_DISC);
			}
			else {
				long num = strtol(value.c_str(), NULL, 10);
				uint32_t volume = (uint32_t)(num * 65536);
				SetLengthTag(XID6_OST_DISC, (uint16_t)num);
			}
		}
		else if (name == "track") {
			if (value.empty()) {
				tags.erase(XID6_OST_TRACK_NUMBER);
			}
			else {
				long track = strtol(value.c_str(), &endptr, 10);
				uint8_t sym = *endptr;

				if (track >= 0 && track <= 255) {
					SetLengthTag(XID6_OST_TRACK_NUMBER, (uint16_t)((track << 8) | sym));
				}
			}
		}
		else if (name == "intro") {
			if (value.empty()) {
				tags.erase(XID6_INTRO_LENGTH);
			}
			else {
				bool valid_format;
				uint32_t ticks = TimeStringToXID6Ticks(value, &valid_format);
				if (valid_format) {
					SetIntegerTag(XID6_INTRO_LENGTH, ticks, 4);
				}
			}
		}
		else if (name == "loop") {
			if (value.empty()) {
				tags.erase(XID6_LOOP_LENGTH);
			}
			else {
				bool valid_format;
				uint32_t ticks = TimeStringToXID6Ticks(value, &valid_format);
				if (valid_format) {
					SetIntegerTag(XID6_LOOP_LENGTH, ticks, 4);
				}
			}
		}
		else if (name == "end") {
			if (value.empty()) {
				tags.erase(XID6_END_LENGTH);
			}
			else {
				bool valid_format;
				uint32_t ticks = TimeStringToXID6Ticks(value, &valid_format);
				if (valid_format) {
					SetIntegerTag(XID6_END_LENGTH, ticks, 4);
				}
			}
		}
		else if (name == "mute") {
			if (value.empty()) {
				tags.erase(XID6_MUTED_VOICES);
			}
			else {
				long num = strtol(value.c_str(), NULL, 10);
				SetLengthTag(XID6_MUTED_VOICES, (uint16_t)num);
			}
		}
		else if (name == "loopcount") {
			if (value.empty()) {
				tags.erase(XID6_LOOP_COUNT);
			}
			else {
				long num = strtol(value.c_str(), NULL, 10);
				SetLengthTag(XID6_LOOP_COUNT, (uint16_t)num);
			}
		}
	}
	return true;
}

std::map<std::string, std::string> SPCFile::ExportPSFTag(bool unofficial_tags) const
{
	std::map<std::string, std::string> psf_tags;

	uint32_t length_in_ticks = GetPlaybackLength();
	if (length_in_ticks != 0) {
		std::string time_string = XID6TicksToTimeString(length_in_ticks, false);
		psf_tags["length"] = time_string;
	}

	for (auto itr = tags.begin(); itr != tags.end(); ++itr) {
		char s[256];

		const XID6ItemId id = (*itr).first;

		switch (id) {
		case XID6_SONG_NAME:
			psf_tags["title"] = GetStringTag(id);
			break;

		case XID6_ARTIST_NAME:
			psf_tags["artist"] = GetStringTag(id);
			break;

		case XID6_GAME_NAME:
			psf_tags["game"] = GetStringTag(id);
			break;

		case XID6_COPYRIGHT_YEAR:
			sprintf(s, "%d", GetIntegerTag(id));
			psf_tags["year"] = s;
			break;

		case XID6_COMMENT:
			psf_tags["comment"] = GetStringTag(id);
			break;

		case XID6_PUBLISHER_NAME:
			psf_tags["copyright"] = GetStringTag(id);
			break;

		case XID6_DUMPER_NAME:
			psf_tags["snsfby"] = GetStringTag(id);
			break;

		case XID6_VOLUME:
		{
			sprintf(s, "%.6f", (double)GetIntegerTag(id) / 65536);

			// trim zeros
			size_t offset = strlen(s) - 1;
			while (offset > 0) {
				if (s[offset] == '0') {
					s[offset] = '\0';
				}
				else {
					if (s[offset] == '.') {
						s[offset] = '\0';
					}
					break;
				}

				offset--;
			}

			psf_tags["volume"] = s;
			break;
		}

		case XID6_FADE_LENGTH:
		{
			uint32_t ticks = GetIntegerTag(id);
			std::string time_string = XID6TicksToTimeString(ticks, false);
			psf_tags["fade"] = time_string;
			break;
		}

		default:
			if (unofficial_tags) {
				switch (id) {
				case XID6_DUMPED_DATE:
				{
					uint32_t yyyymmdd = GetIntegerTag(id);
					uint32_t year = yyyymmdd / 10000;
					uint32_t month = (yyyymmdd / 100) % 100;
					uint32_t day = yyyymmdd % 100;

					sprintf(s, "%d/%02d/%02d", year, month, day);
					psf_tags["created_at"] = s;
					break;
				}

				case XID6_EMULATOR:
					sprintf(s, "%d", GetIntegerTag(id));
					psf_tags["emulator"] = s;
					break;

				case XID6_OST_TITLE:
					psf_tags["soundtrack"] = GetStringTag(id);
					break;

				case XID6_OST_DISC:
					sprintf(s, "%d", GetIntegerTag(id));
					psf_tags["disc"] = s;
					break;

				case XID6_OST_TRACK_NUMBER:
				{
					uint16_t num = GetIntegerTag(id);
					uint8_t track = num >> 8;
					uint8_t sym = num & 0xff;
					sprintf(s, "%d%c", track, sym);
					psf_tags["track"] = s;
					break;
				}

				case XID6_INTRO_LENGTH:
				{
					uint32_t ticks = GetIntegerTag(id);
					std::string time_string = XID6TicksToTimeString(ticks, false);
					psf_tags["intro"] = time_string;
					break;
				}

				case XID6_LOOP_LENGTH:
				{
					uint32_t ticks = GetIntegerTag(id);
					std::string time_string = XID6TicksToTimeString(ticks, false);
					psf_tags["loop"] = time_string;
					break;
				}

				case XID6_END_LENGTH:
				{
					uint32_t ticks = GetIntegerTag(id);
					std::string time_string = XID6TicksToTimeString(ticks, false);
					psf_tags["end"] = time_string;
					break;
				}

				case XID6_MUTED_VOICES:
					sprintf(s, "%d", GetIntegerTag(id));
					psf_tags["mute"] = s;
					break;

				case XID6_LOOP_COUNT:
					sprintf(s, "%d", GetIntegerTag(id));
					psf_tags["loopcount"] = s;
					break;
				}
			}
		}
	}

	return psf_tags;
}

bool SPCFile::ParseDateString(const std::string & str, int & year, int & month, int & day)
{
	int n1, n2, n3;

	if (sscanf(str.c_str(), "%d/%d/%d", &n1, &n2, &n3) != 3 &&
		sscanf(str.c_str(), "%d-%d-%d", &n1, &n2, &n3) != 3 &&
		sscanf(str.c_str(), "%d.%d.%d", &n1, &n2, &n3) != 3) {
		return false;
	}

	if (n1 > 31) {
		// YYYY/MM/DD
		year = n1;
		month = n2;
		day = n3;
	}
	else if (n3 > 31) {
		// MM/DD/YYYY
		year = n3;
		month = n1;
		day = n2;
	}
	else {
		return false;
	}

	return (year >= 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31);
}

void SPCFile::SetTagValue(XID6ItemId id, XID6TypeId type, const uint8_t *binary, size_t size)
{
	std::vector<char> data;
	data.reserve(size);
	for (size_t i = 0; i < size; i++) {
		data.push_back(binary[i]);
	}

	XID6TagItem tag;
	tag.type = type;
	tag.value = data;
	tags[id] = tag;
}
