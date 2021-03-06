#include "Disk.h"
#include <lz4.h>
#include <string.h>
#include <assert.h>
#include <string>

enum
{
	SectorSize = 512
};

static uint64_t magic = 0x6d4936386c756d45ULL;

class IOBase
{
public:
	IOBase() : m_size(0) {}
	virtual ~IOBase() {}
	virtual void Init(const char* path) = 0;
	virtual void Read(char* dst, uint32_t location, uint32_t size) const = 0;
	virtual void Write(const char* dst, uint32_t location, uint32_t size) = 0;
	
	size_t GetSize() const
	{
		return m_size;
	}

	template<typename T>
	T Read(uint32_t location) const
	{
		T data;
		Read((char*)&data, location, sizeof(T));
		return data;
	}

protected:
	size_t m_size;
};

void try_open(FILE*& f, const char* fmt, const char* path)
{
	if (f == nullptr)
	{
		char buffer[256];
		sprintf(buffer, fmt, path);
		f = fopen(buffer, "rb");
	}
}

class IOMemory: public IOBase
{
public:
	IOMemory() : m_data(nullptr) 
	{}

	virtual ~IOMemory()
	{
		delete m_data;
	}

	virtual void Init(const char* path) override
	{
		FILE* file = nullptr;
		try_open(file, "%s.cmp", path);
		try_open(file, "../%s.cmp", path);
		try_open(file, "%s", path);
		try_open(file, "%s.img", path);
		try_open(file, "%s.ima", path);
		try_open(file, "%s.dmg", path);
		try_open(file, "../%s", path);
		try_open(file, "../%s.img", path);
		try_open(file, "../%s.ima", path);
		try_open(file, "../%s.dmg", path);
	
		fseek(file, 0L, SEEK_END);
		m_size = ftell(file);
		fseek(file, 0L, SEEK_SET);

		uint64_t in_magic;
		fread(&in_magic, sizeof(uint64_t), 1, file);

		if (in_magic == magic)
		{
			uint64_t block_size = 1024 * 1024;

			uint64_t bound = (uint64_t)LZ4_compressBound((int)block_size);
			char* data = new char[bound];

			uint64_t decompressed_size = 0;
			fread(&decompressed_size, sizeof(uint64_t), 1, file);
			m_data = new char[decompressed_size + 1024];

			uint64_t p = 0;
			for (p = 0; p != decompressed_size;)
			{
				uint64_t this_block_size = 0;
				uint64_t compressed_size = 0;
				fread(&this_block_size, sizeof(uint64_t), 1, file);
				fread(&compressed_size, sizeof(uint64_t), 1, file);
				fread(data, compressed_size, 1, file);

				for (unsigned int i = 0; i < (compressed_size + sizeof(uint64_t) - 1) / sizeof(uint64_t); ++i)
					reinterpret_cast<uint64_t*>(data)[i] ^= 0xA3F2A5C6B8B48544;

				uint64_t compressed = LZ4_decompress_fast(data, m_data + p, this_block_size);
				assert(compressed == compressed_size);
				p += this_block_size;
			}
			uint64_t size_read = ftell(file);
			assert(m_size == size_read);
			m_size = decompressed_size;
		}
		else
		{
			m_data = new char[m_size + 1024];

			fseek(file, 0L, SEEK_SET);
			fread(m_data, m_size, 1, file);
		}
		fclose(file);
	}

	virtual void Read(char* dst, uint32_t location, uint32_t size) const override
	{
		assert(location + size <= m_size);
		memcpy(dst, m_data + location, size);
	}

	virtual void Write(const char* src, uint32_t location, uint32_t size) override
	{
		assert(location + size <= m_size);
		memcpy(m_data + location, src, size);
	}

private:
	char* m_data;
};

class IODisk: public IOBase
{
public:
	IODisk() : m_file(nullptr)
	{}

	virtual void Init(const char* path) override
	{
		m_file = fopen(path, "rb+");
		fseek(m_file, 0L, SEEK_END);
		m_size = ftell(m_file);
		fseek(m_file, 0L, SEEK_SET);
	}

	~IODisk()
	{
		fclose(m_file);
	}

	virtual void Read(char* dst, uint32_t location, uint32_t size) const override
	{
		assert(location + size <= m_size);
		fseek(m_file, location, SEEK_SET);
		fread(dst, size, 1, m_file);
	}

	virtual void Write(const char* src, uint32_t location, uint32_t size) override
	{
		assert(location + size <= m_size);
		fseek(m_file, location, SEEK_SET);
		fwrite(src, size, 1, m_file);
	}

private:
	FILE* m_file;
};

struct DiskImpl
{
	DiskImpl(bool memory);
	~DiskImpl();

	std::string path;

	IOBase* m_io;

	Disk::BIOS_ParameterBlock m_biosBlock;
	size_t m_size;

	bool m_bootable;
	bool m_floppy;

	uint16_t m_physical_secPerTrk;
	uint16_t m_physical_numHeads;
	bool m_doTranslation;
};

DiskImpl::DiskImpl(bool memory) : m_bootable(false), m_floppy(false), m_io(nullptr)
{
	if (memory)
	{
		m_io = new IOMemory();
	}
	else
	{
		m_io = new IODisk;
	}
}

DiskImpl::~DiskImpl()
{
	delete m_io;
}

Disk::Disk(): m_impl(nullptr)
{}

void Disk::ReadBIOS_ParameterBlock(uint32_t offset)
{
	BIOS_ParameterBlock& block = m_impl->m_biosBlock;
	block.bytsPerSec = m_impl->m_io->Read<uint16_t>(0x00B + offset);
	block.secPerClus = m_impl->m_io->Read<uint8_t>(0x00D + offset);
	block.totSec16 = m_impl->m_io->Read<uint16_t>(0x013 + offset);
	if (block.totSec16 == 0)
	{
		block.totSec16 = m_impl->m_io->Read<uint32_t>(0x020 + offset);
	}
	block.secPerTrk = m_impl->m_io->Read<uint16_t>(0x018 + offset);
	block.numHeads = m_impl->m_io->Read<uint16_t>(0x01A + offset);
	block.drvNum = m_impl->m_io->Read<uint8_t>(0x024 + offset);
	block.volID = m_impl->m_io->Read<uint32_t>(0x027 + offset);
	memset(block.volLabel, 0, sizeof(block.volLabel));
	memset(block.fileSysType, 0, sizeof(block.fileSysType));
	for (int i = 0; i < 11; ++i)
	{
		block.volLabel[i] = m_impl->m_io->Read<uint8_t>(0x2B + i);
	}
	for (int i = 0; i < 8; ++i)
	{
		block.fileSysType[i] = m_impl->m_io->Read<uint8_t>(0x36 + i);
	}
}


void Disk::Open(const char* path, bool inmemory)
{
	m_impl.reset(new DiskImpl(inmemory));

	m_impl->path = path;

	m_impl->m_io->Init(path);

	m_impl->m_size = m_impl->m_io->GetSize();

	// Check for boot sector signature
	m_impl->m_bootable = m_impl->m_io->Read<uint16_t>(0x1FE) == 0xAA55;

	// Check if it is a floppy drive image
	ReadBIOS_ParameterBlock(0);
	BIOS_ParameterBlock& block = m_impl->m_biosBlock;

	if (block.bytsPerSec == SectorSize)
	{
		// Floppy iamge
		m_impl->m_floppy = true;
		m_impl->m_doTranslation = false;
	}
	else
	{
		// Hard disk image

		// Assume 63/16/x geometry for all images
		m_impl->m_physical_secPerTrk = 63;
		m_impl->m_physical_numHeads = 16;
		block.bytsPerSec = SectorSize;
		int cylinders = static_cast<int>(m_impl->m_size / (m_impl->m_physical_numHeads * m_impl->m_physical_secPerTrk * SectorSize));
		int heads = m_impl->m_physical_numHeads;

		while (cylinders > 1024) {
			cylinders >>= 1;
			heads <<= 1;

			// If we max out the head count
			if (heads > 127) break;
		}
		
		// clip to 1024 cylinders in lchs
		if (cylinders > 1024) cylinders = 1024;
		
		block.numHeads = heads;
		block.secPerTrk = 63;
		
		m_impl->m_size = ((size_t)cylinders * block.numHeads * block.secPerTrk * SectorSize);
		block.totSec16 = static_cast<int>(m_impl->m_size / SectorSize);

		m_impl->m_doTranslation = block.numHeads != m_impl->m_physical_numHeads;
	}
}


uint32_t Disk::ToLBA(uint16_t cylinder, uint8_t head, uint8_t sector)
{
	BIOS_ParameterBlock& block = m_impl->m_biosBlock;
	return (cylinder * block.numHeads + head) * block.secPerTrk + sector - 1;
}


bool Disk::Read(char* dst, uint16_t cylinder, uint8_t head, uint8_t sector, uint32_t sectorCount)
{
	uint32_t location = SectorSize * ToLBA(cylinder, head, sector);
	if (location + SectorSize * sectorCount < m_impl->m_size)
	{
		m_impl->m_io->Read(dst, location, sectorCount * SectorSize);
		return true;
	}
	return false;
}


bool Disk::Write(const char* dst, uint16_t cylinder, uint8_t head, uint8_t sector, uint32_t sectorCount)
{
	uint32_t location = SectorSize * ToLBA(cylinder, head, sector);
	if (location + SectorSize * sectorCount < m_impl->m_size)
	{
		m_impl->m_io->Write(dst, location, sectorCount * SectorSize);
		return true;
	}
	return false;
}


bool Disk::IsBootlable() const
{
	return m_impl->m_bootable;
}


bool Disk::IsFloppyDrive() const
{
	return m_impl->m_floppy;
}

void Disk::Close()
{
	m_impl = nullptr;
}

const Disk::BIOS_ParameterBlock& Disk::GetBiosBlock()
{
	return m_impl->m_biosBlock;
}


size_t Disk::size() const
{
	return m_impl->m_size;
}

std::string Disk::GetPath() const
{
	return m_impl->path;
}
