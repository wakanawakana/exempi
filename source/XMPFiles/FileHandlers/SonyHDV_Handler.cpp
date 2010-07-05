// =================================================================================================
// ADOBE SYSTEMS INCORPORATED
// Copyright 2007 Adobe Systems Incorporated
// All Rights Reserved
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.
// =================================================================================================

#include <stdlib.h>

#include "SonyHDV_Handler.hpp"

#include "MD5.h"

#if XMP_WinBuild
	#pragma warning ( disable : 4996 )	// '...' was declared deprecated
#endif

using namespace std;

// =================================================================================================
/// \file SonyHDV_Handler.cpp
/// \brief Folder format handler for Sony HDV.
///
/// This handler is for the Sony HDV video format. This is a pseudo-package, visible files but with
/// a very well-defined layout and naming rules.
///
/// A typical Sony HDV layout looks like:
///
/// .../MyMovie/
/// 	VIDEO/
/// 		HDV/
/// 			00_0001_2007-08-06_165555.IDX
/// 			00_0001_2007-08-06_165555.M2T
/// 			00_0001_2007-08-06_171740.M2T
/// 			00_0001_2007-08-06_171740.M2T.ese
/// 			tracks.dat
///
/// The logical clip name can be "00_0001" or "00_0001_" plus anything. We'll find the .IDX file,
/// which defines the existence of the clip. Full file names as input will pull out the camera/clip
/// parts and match in the same way. The .XMP file will use the date/time suffix from the .IDX file.
// =================================================================================================

// =================================================================================================
// SonyHDV_CheckFormat
// ===================
//
// This version does fairly simple checks. The top level folder (.../MyMovie) must contain the
// VIDEO/HVR subtree. The HVR folder must contain a .IDX file for the desired clip. The name checks
// are case insensitive.
//
// The state of the string parameters depends on the form of the path passed by the client. If the
// client passed a logical clip path, like ".../MyMovie/00_0001", the parameters are:
//   rootPath   - ".../MyMovie"
//   gpName     - empty
//   parentName - empty
//   leafName   - "00_0001"
//
// If the client passed a full file path, like ".../MyMovie/VIDEO/HVR/00_0001_2007-08-06_165555.M2T", 
// they are:
//   rootPath   - ".../MyMovie"
//   gpName     - "VIDEO"
//   parentName - "HVR"
//   leafName   - "00_0001_2007-08-06_165555.M2T"
//
// The logical clip name can be short like "00_0001", or long like "00_0001_2007-08-06_165555". We
// only key off of the portion before a second underscore.

// ! The common code has shifted the gpName, parentName, and leafName strings to upper case. It has
// ! also made sure that for a logical clip path the rootPath is an existing folder, and that the
// ! file exists for a full file path.

bool SonyHDV_CheckFormat ( XMP_FileFormat format,
						   const std::string & rootPath,
						   const std::string & gpName,
						   const std::string & parentName,
						   const std::string & leafName,
						   XMPFiles * parent )
{
	// Do some basic checks on the root path and component names.
	
	if ( gpName.empty() != parentName.empty() ) return false;	// Must be both empty or both non-empty.

	std::string tempPath = rootPath;
	tempPath += kDirChar;
	tempPath += "VIDEO";
	
	if ( gpName.empty() ) {
		// This is the logical clip path case. Look for VIDEO/HVR subtree.
		if ( GetChildMode ( tempPath, "HVR" ) != kFMode_IsFolder ) return false;
	} else {
		// This is the existing file case. Check the parent and grandparent names.
		if ( (gpName != "VIDEO") || (parentName != "HVR") ) return false;
	}
	
	// Look for the clip's .IDX file. If found use that as the full clip name.
	
	tempPath += kDirChar;
	tempPath += "HVR";
	
	std::string clipName = leafName;
	int usCount = 0;
	size_t i, limit = leafName.size();
	for ( i = 0; i < limit; ++i ) {
		if ( clipName[i] == '_' ) {
			++usCount;
			if ( usCount == 2 ) break;
		}
	}
	if ( i < limit ) clipName.erase ( i );
	clipName += '_';	// Make sure a final '_' is there for the search comparisons.

	XMP_FolderInfo folderInfo;
	std::string childName;
	bool found = false;
	
	folderInfo.Open ( tempPath.c_str() );
	while ( (! found) && folderInfo.GetNextChild ( &childName ) ) {
		size_t childLen = childName.size();
		if ( childLen < 4 ) continue;
		MakeUpperCase ( &childName );
		if ( childName.compare ( childLen-4, 4, ".IDX" ) != 0 ) continue;
		if ( childName.compare ( 0, clipName.size(), clipName ) == 0 ) {
			found = true;
			clipName = childName;
			clipName.erase ( childLen-4 );
		}
	}
	if ( ! found ) return false;
	

	// Disabled until Sony HDV clip spanning is supported. 
	// Since segments of spanned clips are currently considered separate entities,
	// information such as frame count needs to be considered on a per segment basis. JPM
	clipName = leafName;
	
	// Set tempPath to <root>/<clip-name>, e.g. ".../MyMovie/00_0001_2007-08-06_165555". This is
	// passed to the handler via the tempPtr hackery.
	
	tempPath = rootPath;
	tempPath += kDirChar;
	tempPath += clipName;
	
	size_t pathLen = tempPath.size() + 1;	// Include a terminating nul.
	parent->tempPtr = malloc ( pathLen );
	if ( parent->tempPtr == 0 ) XMP_Throw ( "No memory for SonyHDV clip info", kXMPErr_NoMemory );
	memcpy ( parent->tempPtr, tempPath.c_str(), pathLen );	// AUDIT: Safe, allocated above.
	
	return true;
	
}	// SonyHDV_CheckFormat

// =================================================================================================
// ReadIDXFile
// ===========

#define ExtractTimeCodeByte(ch,mask)	( (((ch & mask) >> 4) * 10) + (ch & 0xF) )

static bool ReadIDXFile ( const std::string& idxPath, 
						  const std::string& clipName,
						  SXMPMeta* xmpObj, 
						  bool& containsXMP, 
						  MD5_CTX* md5Context,
						  bool digestFound )
{
	bool result = true;
	containsXMP = false;

	if ( clipName.size() != 25 ) return false;

	try {


		AutoFile idxFile;
		idxFile.fileRef = LFA_Open ( idxPath.c_str(), 'r' );
		if ( idxFile.fileRef == 0 ) return false;	// The open failed.

		struct SHDV_HeaderBlock
		{
			char			mHeader[8];
			unsigned char	mValidFlag;
			unsigned char	mReserved;
			unsigned char	mECCTB;
			unsigned char   mSignalMode;
			unsigned char	mFileThousands;
			unsigned char	mFileHundreds;
			unsigned char	mFileTens;
			unsigned char	mFileUnits;
		};

		SHDV_HeaderBlock hdvHeaderBlock;
		memset ( &hdvHeaderBlock, 0, sizeof(SHDV_HeaderBlock) );

		LFA_Read ( idxFile.fileRef, hdvHeaderBlock.mHeader, 8 );
		LFA_Read ( idxFile.fileRef, &hdvHeaderBlock.mValidFlag, 1 );
		LFA_Read ( idxFile.fileRef, &hdvHeaderBlock.mReserved, 1 );
		LFA_Read ( idxFile.fileRef, &hdvHeaderBlock.mECCTB, 1 );
		LFA_Read ( idxFile.fileRef, &hdvHeaderBlock.mSignalMode, 1 );
		LFA_Read ( idxFile.fileRef, &hdvHeaderBlock.mFileThousands, 1 );
		LFA_Read ( idxFile.fileRef, &hdvHeaderBlock.mFileHundreds, 1 );
		LFA_Read ( idxFile.fileRef, &hdvHeaderBlock.mFileTens, 1 );
		LFA_Read ( idxFile.fileRef, &hdvHeaderBlock.mFileUnits, 1 );

		const int fileCount = (hdvHeaderBlock.mFileThousands - '0') * 1000 +
							  (hdvHeaderBlock.mFileHundreds  - '0') *  100 +
							  (hdvHeaderBlock.mFileTens      - '0') *   10 +
							  (hdvHeaderBlock.mFileUnits     - '0');

		// Read file info block.
		struct	SHDV_FileBlock
		{
			char			mDT[2];
			unsigned char	mFileNameYear;
			unsigned char	mFileNameMonth;
			unsigned char	mFileNameDay;
			unsigned char	mFileNameHour;
			unsigned char	mFileNameMinute;
			unsigned char	mFileNameSecond;
			unsigned char	mStartTimeCode[4];
			unsigned char	mTotalFrame[4];
		};

		SHDV_FileBlock hdvFileBlock;
		memset ( &hdvFileBlock, 0, sizeof(SHDV_FileBlock) );

		char filenameBuffer[256];
		std::string fileDateAndTime = clipName.substr(8);

		bool foundFileBlock = false;

		for ( int i=0; ((i < fileCount) && (! foundFileBlock)); ++i ) {

			LFA_Read ( idxFile.fileRef, hdvFileBlock.mDT, 2 );
			LFA_Read ( idxFile.fileRef, &hdvFileBlock.mFileNameYear, 1 );
			LFA_Read ( idxFile.fileRef, &hdvFileBlock.mFileNameMonth, 1 );
			LFA_Read ( idxFile.fileRef, &hdvFileBlock.mFileNameDay, 1 );
			LFA_Read ( idxFile.fileRef, &hdvFileBlock.mFileNameHour, 1 );
			LFA_Read ( idxFile.fileRef, &hdvFileBlock.mFileNameMinute, 1 );
			LFA_Read ( idxFile.fileRef, &hdvFileBlock.mFileNameSecond, 1 );
			LFA_Read ( idxFile.fileRef, &hdvFileBlock.mStartTimeCode, 4 );
			LFA_Read ( idxFile.fileRef, &hdvFileBlock.mTotalFrame, 4 );

			// Compose file name we expect from file contents and break out on match.
			sprintf ( filenameBuffer, "%02d-%02d-%02d_%02d%02d%02d", 
					  hdvFileBlock.mFileNameYear + 2000,
					  hdvFileBlock.mFileNameMonth, 
					  hdvFileBlock.mFileNameDay, 
					  hdvFileBlock.mFileNameHour, 
					  hdvFileBlock.mFileNameMinute, 
					  hdvFileBlock.mFileNameSecond );

			foundFileBlock = (fileDateAndTime==filenameBuffer);

		}

		LFA_Close ( idxFile.fileRef );
		idxFile.fileRef = 0;

		if ( ! foundFileBlock ) return false;

		// If digest calculation requested, calculate it and return.
		if ( md5Context != 0 ) {
			MD5Update ( md5Context, (XMP_Uns8*)(&hdvHeaderBlock), sizeof(SHDV_HeaderBlock) );
			MD5Update ( md5Context, (XMP_Uns8*)(&hdvFileBlock), sizeof(SHDV_FileBlock) );
		}

		// The xmpObj parameter must be provided in order to extract XMP
		if ( xmpObj == 0 ) return (md5Context != 0);

		// Standard def?
		const bool isSD = ((hdvHeaderBlock.mSignalMode == 0x80) || (hdvHeaderBlock.mSignalMode == 0));

		// Progressive vs interlaced extracted from high bit of ECCTB byte
		const bool clipIsProgressive = ((hdvHeaderBlock.mECCTB & 0x80) != 0);

		// Lowest three bits contain frame rate information
		const int sfr = (hdvHeaderBlock.mECCTB & 7) + (clipIsProgressive ? 0 : 8);

		// Sample scale and sample size.
		int clipSampleScale = 0;
		int clipSampleSize  = 0;
		std::string frameRate;

		// Frame rate
		switch ( sfr ) {
			case 0	: break; // Not valid in spec, but it's happening in test files.
			case 1	: clipSampleScale = 24000;	clipSampleSize = 1001; frameRate = "23.98p"; break;
			case 3	: clipSampleScale = 25;		clipSampleSize = 1;	   frameRate =    "25p"; break;
			case 4	: clipSampleScale = 30000;	clipSampleSize = 1001; frameRate = "29.97p"; break;		
			case 11	: clipSampleScale = 25;		clipSampleSize = 1;	   frameRate =    "50i"; break;
			case 12	: clipSampleScale = 30000;	clipSampleSize = 1001; frameRate = "59.94i"; break;		
		}

		containsXMP = true;

		// Frame size and PAR for HD (not clear on SD yet).
		std::string xmpString;
		XMP_StringPtr xmpValue = 0; 

		if ( ! isSD ) {

			if ( digestFound || (! xmpObj->DoesPropertyExist ( kXMP_NS_DM, "videoFrameSize" )) ) {

				xmpValue = "1440";
				xmpObj->GetStructField ( kXMP_NS_DM, "videoFrameSize", kXMP_NS_DM, "w", &xmpString, 0 );
				if ( xmpString != xmpValue ) {
					xmpObj->SetStructField ( kXMP_NS_DM, "videoFrameSize", kXMP_NS_XMP_Dimensions, "w", xmpValue, 0 );
				}

				xmpValue = "1080";
				xmpObj->GetStructField ( kXMP_NS_DM, "videoFrameSize", kXMP_NS_DM, "h", &xmpString, 0 );
				if ( xmpString != xmpValue ) {
					xmpObj->SetStructField ( kXMP_NS_DM, "videoFrameSize", kXMP_NS_XMP_Dimensions, "h", xmpValue, 0 );
				}

				xmpValue = "pixels";
				xmpObj->GetStructField ( kXMP_NS_DM, "videoFrameSize", kXMP_NS_DM, "unit", &xmpString, 0 );
				if ( xmpString != xmpValue ) {
					xmpObj->SetStructField ( kXMP_NS_DM, "videoFrameSize", kXMP_NS_XMP_Dimensions, "unit", xmpValue, 0 );
				}
			}

			xmpValue = "4/3";
			if ( digestFound || (! xmpObj->DoesPropertyExist ( kXMP_NS_DM, "videoPixelAspectRatio" )) ) {
				xmpObj->SetProperty ( kXMP_NS_DM, "videoPixelAspectRatio", xmpValue, kXMP_DeleteExisting );
			}

		}

		// Sample size and scale.
		if ( clipSampleScale != 0 ) {

			char buffer[255];

			if ( digestFound || (! xmpObj->DoesPropertyExist ( kXMP_NS_DM, "startTimeScale" )) ) {
				sprintf(buffer, "%d", clipSampleScale);
				xmpValue = buffer; 
				xmpObj->SetProperty ( kXMP_NS_DM, "startTimeScale", xmpValue, kXMP_DeleteExisting );
			}

			if ( digestFound || (! xmpObj->DoesPropertyExist ( kXMP_NS_DM, "startTimeSampleSize" )) ) {
				sprintf(buffer, "%d", clipSampleSize);
				xmpValue = buffer; 
				xmpObj->SetProperty ( kXMP_NS_DM, "startTimeSampleSize", xmpValue, kXMP_DeleteExisting );
			}

			if ( digestFound || (! xmpObj->DoesPropertyExist ( kXMP_NS_DM, "duration" )) ) {

				const int frameCount = (hdvFileBlock.mTotalFrame[0] << 24) + (hdvFileBlock.mTotalFrame[1] << 16) +
									   (hdvFileBlock.mTotalFrame[2] << 8) + hdvFileBlock.mTotalFrame[3];

				sprintf ( buffer, "%d", frameCount );
				xmpValue = buffer; 
				xmpObj->SetStructField ( kXMP_NS_DM, "duration", kXMP_NS_DM, "value", xmpValue, 0 );

				sprintf ( buffer, "%d/%d", clipSampleSize, clipSampleScale );
				xmpValue = buffer; 
				xmpObj->SetStructField ( kXMP_NS_DM, "duration", kXMP_NS_DM, "scale", xmpValue, 0 );

			}		

		}

		// Time Code.
		if ( digestFound || (! xmpObj->DoesPropertyExist ( kXMP_NS_DM, "startTimecode" )) ) {

			if ( (clipSampleScale != 0) && (clipSampleSize != 0) ) {

				const bool dropFrame = ( (0x40 & hdvFileBlock.mStartTimeCode[0]) != 0 ) && ( sfr == 4 || sfr == 12 );
				const char chDF = dropFrame ? ';' : ':';
				const int tcFrames  = ExtractTimeCodeByte ( hdvFileBlock.mStartTimeCode[0], 0x30 );
				const int tcSeconds = ExtractTimeCodeByte ( hdvFileBlock.mStartTimeCode[1], 0x70 );
				const int tcMinutes = ExtractTimeCodeByte ( hdvFileBlock.mStartTimeCode[2], 0x70 );
				const int tcHours   = ExtractTimeCodeByte ( hdvFileBlock.mStartTimeCode[3], 0x30 );

				// HH:MM:SS:FF or HH;MM;SS;FF
				char timecode[256];
				sprintf ( timecode, "%02d%c%02d%c%02d%c%02d", tcHours, chDF, tcMinutes, chDF, tcSeconds, chDF, tcFrames );
				std::string sonyTimeString = timecode;
		 
				xmpObj->GetStructField ( kXMP_NS_DM, "startTimecode", kXMP_NS_DM, "timeValue", &xmpString, 0 );
				if ( xmpString != sonyTimeString ) {

					xmpObj->SetStructField ( kXMP_NS_DM, "startTimecode", kXMP_NS_DM, "timeValue", sonyTimeString, 0 );

					std::string timeFormat;
					if ( clipSampleSize == 1 ) {	

						// 24, 25, 40, 50, 60
						switch ( clipSampleScale ) {
							case 24 : timeFormat = "24"; break;
							case 25 : timeFormat = "25"; break;
							case 50 : timeFormat = "50"; break;
							default : XMP_Assert ( false );
						}

						timeFormat += "Timecode";

					} else {

						// 23.976, 29.97, 59.94
						XMP_Assert ( clipSampleSize == 1001 );
						switch ( clipSampleScale ) {
							case 24000 : timeFormat = "23976"; break;
							case 30000 : timeFormat =  "2997"; break;
							case 60000 : timeFormat =  "5994"; break;
							default	   : XMP_Assert( false );  break;
						}

						timeFormat += dropFrame ? "DropTimecode" : "NonDropTimecode";

					}		

					xmpObj->SetStructField ( kXMP_NS_DM, "startTimecode", kXMP_NS_DM, "timeFormat", timeFormat, 0 );

				}

			}

		}

		if ( digestFound || (! xmpObj->DoesPropertyExist ( kXMP_NS_DM, "CreateDate" )) ) {

			// Clip has date and time in the case of DT (otherwise date and time haven't been set).
			bool clipHasDate = ((hdvFileBlock.mDT[0] == 'D') && (hdvFileBlock.mDT[1] == 'T'));

			// Creation date
			if ( clipHasDate ) {

				// YYYY-MM-DDThh:mm:ssZ
				char date[256];
				sprintf ( date, "%4d-%02d-%02dT%02d:%02d:%02dZ", 
						  hdvFileBlock.mFileNameYear + 2000,
						  hdvFileBlock.mFileNameMonth,
						  hdvFileBlock.mFileNameDay,
						  hdvFileBlock.mFileNameHour,
						  hdvFileBlock.mFileNameMinute,
						  hdvFileBlock.mFileNameSecond );

				XMP_StringPtr xmpDate = date;
				xmpObj->SetProperty ( kXMP_NS_XMP, "CreateDate", xmpDate, kXMP_DeleteExisting );

			}

		}

		// Frame rate.
		if ( digestFound || (! xmpObj->DoesPropertyExist ( kXMP_NS_DM, "videoFrameRate" )) ) {

			if ( frameRate.size() != 0 ) {
				xmpString = frameRate;
				xmpObj->SetProperty ( kXMP_NS_DM, "videoFrameRate", xmpString, kXMP_DeleteExisting );
			}

		}

	} catch ( ... ) {

		result = false;

	}

	return result;

}	// ReadIDXFile

// =================================================================================================
// SonyHDV_MetaHandlerCTor
// =======================

XMPFileHandler * SonyHDV_MetaHandlerCTor ( XMPFiles * parent )
{
	return new SonyHDV_MetaHandler ( parent );
	
}	// SonyHDV_MetaHandlerCTor

// =================================================================================================
// SonyHDV_MetaHandler::SonyHDV_MetaHandler
// ========================================

SonyHDV_MetaHandler::SonyHDV_MetaHandler ( XMPFiles * _parent )
{

	this->parent = _parent;	// Inherited, can't set in the prefix.
	this->handlerFlags = kSonyHDV_HandlerFlags;
	this->stdCharForm  = kXMP_Char8Bit;
	
	// Extract the root path and clip name.
	
	XMP_Assert ( this->parent->tempPtr != 0 );
	
	this->rootPath.assign ( (char*) this->parent->tempPtr );
	free ( this->parent->tempPtr );
	this->parent->tempPtr = 0;

	SplitLeafName ( &this->rootPath, &this->clipName );
	
}	// SonyHDV_MetaHandler::SonyHDV_MetaHandler

// =================================================================================================
// SonyHDV_MetaHandler::~SonyHDV_MetaHandler
// =========================================

SonyHDV_MetaHandler::~SonyHDV_MetaHandler()
{

	if ( this->parent->tempPtr != 0 ) {
		free ( this->parent->tempPtr );
		this->parent->tempPtr = 0;
	}

}	// SonyHDV_MetaHandler::~SonyHDV_MetaHandler

// =================================================================================================
// SonyHDV_MetaHandler::MakeClipFilePath
// =====================================

void SonyHDV_MetaHandler::MakeClipFilePath ( std::string * path, XMP_StringPtr suffix )
{

	*path = this->rootPath;
	*path += kDirChar;
	*path += "VIDEO";
	*path += kDirChar;
	*path += "HVR";
	*path += kDirChar;
	*path += this->clipName;
	*path += suffix;

}	// SonyHDV_MetaHandler::MakeClipFilePath

// =================================================================================================
// SonyHDV_MetaHandler::MakeIndexFilePath
// ======================================

bool SonyHDV_MetaHandler::MakeIndexFilePath ( std::string& idxPath, const std::string& rootPath, const std::string& leafName )
{
	std::string tempPath;
	tempPath = rootPath;
	tempPath += kDirChar;
	tempPath += "VIDEO";
	tempPath += kDirChar;
	tempPath += "HVR";

	idxPath = tempPath;
	idxPath += kDirChar;
	idxPath += leafName;
	idxPath += ".IDX";

	// Default case 
	if ( GetFileMode ( idxPath.c_str() ) == kFMode_IsFile ) return true;

	// Spanned clip case

	// Scanning code taken from SonyHDV_CheckFormat
	// Can be isolated to a separate function.

	std::string clipName = leafName;
	int usCount = 0;
	size_t i, limit = leafName.size();

	for ( i = 0; i < limit; ++i ) {
		if ( clipName[i] == '_' ) {
			++usCount;
			if ( usCount == 2 ) break;
		}
	}

	if ( i < limit ) clipName.erase ( i );
	clipName += '_';	// Make sure a final '_' is there for the search comparisons.

	XMP_FolderInfo folderInfo;
	std::string childName;
	bool found = false;

	folderInfo.Open ( tempPath.c_str() );
	while ( (! found) && folderInfo.GetNextChild ( &childName ) ) {
		size_t childLen = childName.size();
		if ( childLen < 4 ) continue;
		MakeUpperCase ( &childName );
		if ( childName.compare ( childLen-4, 4, ".IDX" ) != 0 ) continue;
		if ( childName.compare ( 0, clipName.size(), clipName ) == 0 ) {
			found = true;
			clipName = childName;
			clipName.erase ( childLen-4 );
		}
	}
	if ( ! found ) return false;

	idxPath = tempPath;
	idxPath += kDirChar;
	idxPath += clipName;
	idxPath += ".IDX";

	return true;
}

// =================================================================================================
// SonyHDV_MetaHandler::MakeLegacyDigest
// =====================================

#define kHexDigits "0123456789ABCDEF"

void SonyHDV_MetaHandler::MakeLegacyDigest ( std::string * digestStr )
{
	std::string idxPath;
	if ( ! this->MakeIndexFilePath ( idxPath, this->rootPath, this->clipName ) ) return;

	MD5_CTX context;
	unsigned char digestBin [16];
	bool dummy = false;
	MD5Init ( &context );
	ReadIDXFile ( idxPath, this->clipName, 0,  dummy, &context, false );
	MD5Final ( digestBin, &context );

	char buffer [40];
	for ( int in = 0, out = 0; in < 16; in += 1, out += 2 ) {
		XMP_Uns8 byte = digestBin[in];
		buffer[out]   = kHexDigits [ byte >> 4 ];
		buffer[out+1] = kHexDigits [ byte & 0xF ];
	}
	buffer[32] = 0;
	digestStr->erase();
	digestStr->append ( buffer, 32 );

}	// MakeLegacyDigest

// =================================================================================================
// SonyHDV_MetaHandler::CacheFileData
// ==================================

void SonyHDV_MetaHandler::CacheFileData()
{
	XMP_Assert ( ! this->containsXMP );
	
	// See if the clip's .XMP file exists.
	
	std::string xmpPath;
	this->MakeClipFilePath ( &xmpPath, ".XMP" );
	if ( GetFileMode ( xmpPath.c_str() ) != kFMode_IsFile ) return;	// No XMP.
	
	// Read the entire .XMP file.
	
	char openMode = 'r';
	if ( this->parent->openFlags & kXMPFiles_OpenForUpdate ) openMode = 'w';
	
	LFA_FileRef xmpFile = LFA_Open ( xmpPath.c_str(), openMode );
	if ( xmpFile == 0 ) return;	// The open failed.
	
	XMP_Int64 xmpLen = LFA_Measure ( xmpFile );
	if ( xmpLen > 100*1024*1024 ) {
		XMP_Throw ( "SonyHDV XMP is outrageously large", kXMPErr_InternalFailure );	// Sanity check.
	}
	
	this->xmpPacket.erase();
	this->xmpPacket.reserve ( (size_t)xmpLen );
	this->xmpPacket.append ( (size_t)xmpLen, ' ' );

	XMP_Int32 ioCount = LFA_Read ( xmpFile, (void*)this->xmpPacket.data(), (XMP_Int32)xmpLen, kLFA_RequireAll );
	XMP_Assert ( ioCount == xmpLen );
	
	this->packetInfo.offset = 0;
	this->packetInfo.length = (XMP_Int32)xmpLen;
	FillPacketInfo ( this->xmpPacket, &this->packetInfo );
	
	XMP_Assert ( this->parent->fileRef == 0 );
	if ( openMode == 'r' ) {
		LFA_Close ( xmpFile );
	} else {
		this->parent->fileRef = xmpFile;
	}
	
	this->containsXMP = true;
	
}	// SonyHDV_MetaHandler::CacheFileData

// =================================================================================================
// SonyHDV_MetaHandler::ProcessXMP
// ===============================

void SonyHDV_MetaHandler::ProcessXMP()
{
	if ( this->processedXMP ) return;
	this->processedXMP = true;	// Make sure only called once.
	
	if ( this->containsXMP ) {
		this->xmpObj.ParseFromBuffer ( this->xmpPacket.c_str(), (XMP_StringLen)this->xmpPacket.size() );
	}

	// Check the legacy digest.
	std::string oldDigest, newDigest;	
	bool digestFound;	
	digestFound = this->xmpObj.GetStructField ( kXMP_NS_XMP, "NativeDigests", kXMP_NS_XMP, "SonyHDV", &oldDigest, 0 );
	if ( digestFound ) {
		this->MakeLegacyDigest ( &newDigest );
		if ( oldDigest == newDigest ) return;
	}

	// Read the IDX legacy.	
	std::string idxPath;
	if ( ! this->MakeIndexFilePath ( idxPath, this->rootPath, this->clipName ) ) return;
	ReadIDXFile ( idxPath, this->clipName, &this->xmpObj,  this->containsXMP, 0, digestFound );

}	// SonyHDV_MetaHandler::ProcessXMP

// =================================================================================================
// SonyHDV_MetaHandler::UpdateFile
// ===============================
//
// Note that UpdateFile is only called from XMPFiles::CloseFile, so it is OK to close the file here.

void SonyHDV_MetaHandler::UpdateFile ( bool doSafeUpdate )
{
	if ( ! this->needsUpdate ) return;
	this->needsUpdate = false;	// Make sure only called once.
	
	std::string newDigest;
	this->MakeLegacyDigest ( &newDigest );
	this->xmpObj.SetStructField ( kXMP_NS_XMP, "NativeDigests", kXMP_NS_XMP, "SonyHDV", newDigest.c_str(), kXMP_DeleteExisting );

	LFA_FileRef oldFile = this->parent->fileRef;
	
	this->xmpObj.SerializeToBuffer ( &this->xmpPacket, this->GetSerializeOptions() );

	if ( oldFile == 0 ) {
	
		// The XMP does not exist yet.

		std::string xmpPath;
		this->MakeClipFilePath ( &xmpPath, ".XMP" );
		
		LFA_FileRef xmpFile = LFA_Create ( xmpPath.c_str() );
		if ( xmpFile == 0 ) XMP_Throw ( "Failure creating SonyHDV XMP file", kXMPErr_ExternalFailure );
		LFA_Write ( xmpFile, this->xmpPacket.data(), (XMP_StringLen)this->xmpPacket.size() );
		LFA_Close ( xmpFile );
	
	} else if ( ! doSafeUpdate ) {

		// Over write the existing XMP file.
		
		LFA_Seek ( oldFile, 0, SEEK_SET );
		LFA_Truncate ( oldFile, 0 );
		LFA_Write ( oldFile, this->xmpPacket.data(), (XMP_StringLen)this->xmpPacket.size() );
		LFA_Close ( oldFile );

	} else {

		// Do a safe update.
		
		// *** We really need an LFA_SwapFiles utility.
		
		std::string xmpPath, tempPath;
		
		this->MakeClipFilePath ( &xmpPath, ".XMP" );
		
		CreateTempFile ( xmpPath, &tempPath );
		LFA_FileRef tempFile = LFA_Open ( tempPath.c_str(), 'w' );
		LFA_Write ( tempFile, this->xmpPacket.data(), (XMP_StringLen)this->xmpPacket.size() );
		LFA_Close ( tempFile );
		
		LFA_Close ( oldFile );
		LFA_Delete ( xmpPath.c_str() );
		LFA_Rename ( tempPath.c_str(), xmpPath.c_str() );

	}

	this->parent->fileRef = 0;
	
}	// SonyHDV_MetaHandler::UpdateFile

// =================================================================================================
// SonyHDV_MetaHandler::WriteFile
// ==============================

void SonyHDV_MetaHandler::WriteFile ( LFA_FileRef sourceRef, const std::string & sourcePath )
{

	// ! WriteFile is not supposed to be called for handlers that own the file.
	XMP_Throw ( "SonyHDV_MetaHandler::WriteFile should not be called", kXMPErr_InternalFailure );
	
}	// SonyHDV_MetaHandler::WriteFile

// =================================================================================================
