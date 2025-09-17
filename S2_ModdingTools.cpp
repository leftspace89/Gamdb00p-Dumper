#pragma warning( disable : 4786 )  
#pragma warning( disable : 4503 )  
#pragma warning( disable : 4530 )  

#include "platform.h"
#if defined( PLATFORM_WIN32 )
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "engine.h"

#include <stdio.h>
#include <limits.h>
#include <malloc.h>

#include "DebugNew.h"

#include "BuildDefines.h"
#ifdef _CLIENTBUILD
#include "clientheaders.h" 
#endif
#include "iltclient.h"
#include "iltserver.h"
#include "iltmessage.h"
#include "iltsoundmgr.h"
#include "Globals.h"
#include "iltmodel.h"
#include "iltphysics.h"
#include "ltobjectcreate.h"
#include "ltbasetypes.h"
#include "ltobjref.h"
#include "Factory.h"

#ifdef _SERVERBUILD
#include "../ObjectDLL/ServerUtilities.h"
#include "../ObjectDLL/GameServerShell.h"
#endif
#ifdef _CLIENTBUILD

#endif
#include "CommonUtilities.h"
#include "AutoMessage.h"

#if defined(PLATFORM_SEM)
#define GAMESPY_EXPORTS
#endif

#if defined(PLATFORM_LINUX)
#include <sys/linux/linux_stlcompat.h>
#endif 

#include <iostream>

#include "idatabasemgr.h"
#include "idatabasecreatormgr.h"
#include "ltlibraryloader.h"
#include "ltfileoperations.h"
#include "ltprofileutils.h"
#include "ltstrutils.h"
#include "iltfilemgr.h"
#include <ltoutnullconverter.h>
#include "iltmessage.h"
#include "ltengineobjects.h"
#include <stdio.h>
#include "iltsoundmgr.h"
#include "iltcommon.h"
#include "iltphysics.h"
#include "ltserverobj.h"
#include "iobjectplugin.h"
#include "ltobjectcreate.h"
#include "iltserver.h"
#include "server_interface.h"
#include <CLTFileToILTInStream.h>

#include <string>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <functional>

static uint32	 s_nGameDatabaseRef = 0;
static HLTMODULE s_hDatabaseInst = NULL;

IDatabaseMgr* g_pLTDatabase = NULL;
IDatabaseCreatorMgr* g_pLTDatabaseCreator = NULL;
ILTServer* g_pLTServer = NULL;

define_holder_to_instance(ILTServer, g_pLTServer, Default);
define_holder_to_instance(ILTCommon, g_pCommonLT, Server);

HDATABASE OpenGameDatabaseFile(const char* pszFilename)
{
	HDATABASE hDatabase = NULL;

	if ((hDatabase = g_pLTDatabase->OpenExistingDatabase(pszFilename)) == NULL)
	{

		CLTFileToILTInStream InFile;
		if (!InFile.Open(pszFilename))
			return NULL;
		hDatabase = g_pLTDatabase->OpenNewDatabase(pszFilename, InFile);

		if (!hDatabase)
		{

			return NULL;
		}

	}

	return hDatabase;
}

#undef min
#undef max

std::string Escape(const char* str) {
	return str ? str : "";
}

void WriteIniFile(const std::string& path, const std::string& content) {
	std::ofstream out(path, std::ios::binary);
	out << "\xFF\xFE";
	for (char c : content) {
		out.put(c);
		out.put('\0');
	}
}

std::vector<std::string> SplitCategoryName(const std::string& name) {
	std::vector<std::string> parts;
	size_t start = 0;
	size_t dot;
	while ((dot = name.find('.', start)) != std::string::npos) {
		parts.push_back(name.substr(start, dot - start));
		start = dot + 1;
	}
	parts.push_back(name.substr(start));
	return parts;
}

struct AttrInfo {
	std::string fullName;
	std::vector<std::string> toks;
	EAttributeType type;
	uint32 numVals;
	HATTRIBUTE hAttr;
};

static std::vector<std::string> SplitTokens(const std::string& s) {
	std::vector<std::string> out;
	size_t start = 0;
	while (start < s.size()) {
		size_t pos = s.find('.', start);
		if (pos == std::string::npos) pos = s.size();
		out.emplace_back(s.substr(start, pos - start));
		start = pos + (pos < s.size() ? 1 : 0);
	}
	return out;
}
static bool IsAllDigits(const std::string& t) {
	if (t.empty()) return false;
	return std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); });
}

static bool MatchPrefix(const std::vector<std::string>& tokens, const std::vector<std::string>& prefix, size_t& outPosAfter) {
	size_t pos = 0;
	for (const auto& p : prefix) {

		while (pos < tokens.size() && tokens[pos] != p) ++pos;
		if (pos >= tokens.size()) return false;
		++pos;
	}
	outPosAfter = pos;
	return true;
}

static size_t FindNextNonNumeric(const std::vector<std::string>& tokens, size_t pos) {
	while (pos < tokens.size() && IsAllDigits(tokens[pos])) ++pos;
	if (pos >= tokens.size()) return std::string::npos;
	return pos;
}

void DumpDatabaseRecursive(HDATABASE hDB, const std::filesystem::path& outRoot) {
	uint32 numCategories = g_pLTDatabase->GetNumCategories(hDB);
	for (uint32 ci = 0; ci < numCategories; ++ci) {
		HCATEGORY hCat = g_pLTDatabase->GetCategoryByIndex(hDB, ci);
		if (!hCat) continue;

		std::string catName = g_pLTDatabase->GetCategoryName(hCat);
		std::filesystem::path catPath = outRoot;
		std::string schemaName;

		size_t start = 0, end = 0;
		bool first = true;
		while ((end = catName.find('/', start)) != std::string::npos) {
			std::string part = catName.substr(start, end - start);
			catPath /= part;
			schemaName += (first ? "" : ".") + part;
			first = false;
			start = end + 1;
		}
		std::string lastPart = catName.substr(start);
		catPath /= lastPart;
		schemaName += (first ? "" : ".") + lastPart;

		std::filesystem::create_directories(catPath);

		{
			std::ostringstream catIni;
			catIni << "[Category]\n";
			catIni << "Schema=" << schemaName << "\n";
			catIni << "Comment=\nHelp=\nSystem=False\n";
			WriteIniFile((catPath / "gdb.category").string(), catIni.str());
		}

		std::vector<AttrInfo> attrInfos;

		std::ostringstream schemaIni;
		schemaIni << "[Schema]\n";
		schemaIni << "Name=" << schemaName << "\n";
		schemaIni << "Help=\n";

		std::string parentSchema;
		if (schemaName.find('.') != std::string::npos) parentSchema = schemaName.substr(0, schemaName.find_last_of('.'));
		schemaIni << "Parent=" << (parentSchema.empty() ? "" : parentSchema) << "\n";

		std::unordered_set<std::string> addedAttrs;

		uint32 numRecords = g_pLTDatabase->GetNumRecords(hCat);
		for (uint32 ri = 0; ri < numRecords; ++ri) {
			HRECORD hRec = g_pLTDatabase->GetRecordByIndex(hCat, ri);
			if (!hRec) continue;

			const char* recNameC = g_pLTDatabase->GetRecordName(hRec);
			std::string recName = recNameC ? recNameC : "<noname>";

			std::ostringstream recIni;
			recIni << "[Record]\n";
			recIni << "Schema=" << schemaName << "\n";
			recIni << "Name=" << recName << "\n";
			recIni << "Comment=\nVirtualRelativeCategory=\n";

			uint32 numAttrs = g_pLTDatabase->GetNumAttributes(hRec);
			for (uint32 ai = 0; ai < numAttrs; ++ai) {
				HATTRIBUTE hAttr = g_pLTDatabase->GetAttributeByIndex(hRec, ai);
				if (!hAttr) continue;

				const char* attrNameC = g_pLTDatabase->GetAttributeName(hAttr);
				std::string attrName = attrNameC ? attrNameC : "";

				EAttributeType type = g_pLTDatabase->GetAttributeType(hAttr);
				uint32 numVals = g_pLTDatabase->GetNumValues(hAttr);

				AttrInfo info;
				info.fullName = attrName;
				info.toks = SplitTokens(attrName);
				info.type = type;
				info.numVals = numVals;
				info.hAttr = hAttr;
				attrInfos.push_back(std::move(info));

				auto toks = SplitTokens(attrName);
				if (toks.size() == 1) {

					if (addedAttrs.find(attrName) == addedAttrs.end()) {
						addedAttrs.insert(attrName);

						if (type == eAttributeType_Struct) {

							schemaIni << "\n[Attrib." << attrName << ".0]\n";
							schemaIni << "Type=Struct\n";

							schemaIni << "Data=" << attrName << ".struct\n";
							schemaIni << "Default=\nUnicode=False\nDeleted=False\nInherit=True\n";
							schemaIni << "Values=-1\nHelp=\n";
						}
						else {

							schemaIni << "\n[Attrib." << attrName << ".0]\n";
							switch (type) {
							case eAttributeType_Int32: schemaIni << "Type=Integer\nDefault=0\n"; break;
							case eAttributeType_Float: schemaIni << "Type=Float\nDefault=0.0\n"; break;
							case eAttributeType_Bool:  schemaIni << "Type=Boolean\nDefault=False\n"; break;
							case eAttributeType_String: schemaIni << "Type=Text\nUnicode=False\nDefault=\n"; break;
							case eAttributeType_WString: schemaIni << "Type=Text\nUnicode=True\nDefault=\n"; break;
							case eAttributeType_Vector2: schemaIni << "Type=Vector2\nDefault=\n"; break;
							case eAttributeType_Vector3: schemaIni << "Type=Vector3\nDefault=\n"; break;
							case eAttributeType_Vector4: schemaIni << "Type=Vector4\nDefault=\n"; break;
							case eAttributeType_RecordLink:
								schemaIni << "Type=RecordLink\nDefault=<none>\n";
								{

									std::string linkSchema;
									if (numVals > 0) {
										HRECORD hLink = g_pLTDatabase->GetRecordLink(hAttr, 0, nullptr);
										if (hLink) {
											HCATEGORY hLinkCat = g_pLTDatabase->GetRecordParent(hLink);
											if (hLinkCat) {
												linkSchema = g_pLTDatabase->GetCategoryName(hLinkCat);
												std::replace(linkSchema.begin(), linkSchema.end(), '/', '.');
											}
										}
									}
									schemaIni << "Data=" << (linkSchema.empty() ? "" : linkSchema) << "\n";
								}
								break;
							default: schemaIni << "Type=Unknown\nDefault=\n"; break;
							}
							schemaIni << "Inherit=True\nDeleted=False\n";
							schemaIni << "Values=" << (type == eAttributeType_RecordLink ? -1 : (numVals ? numVals : 1)) << "\nHelp=\n";
						}
					}

					std::ostringstream sectionName;
					sectionName << "Attrib";
					for (size_t t = 0; t < toks.size(); ++t) {
						sectionName << "." << toks[t];

						if (IsAllDigits(toks[t])) continue;
						if (t + 1 < toks.size() && IsAllDigits(toks[t + 1])) {
							sectionName << "." << toks[t + 1];
							++t;
						}
						else {
							sectionName << ".0";
						}
					}

					recIni << "\n[" << sectionName.str() << "]\n";
					recIni << "Inherit=False\nPlaceHolder=False\nTodo=False\nModified=False\nOverride=False\n";
					recIni << "Lock=\nComment=\n";

					for (uint32 vi = 0; vi < numVals; ++vi) {
						char key[32]; sprintf(key, "Value.%04u", vi);
						switch (type) {
						case eAttributeType_Int32:
							recIni << key << "=" << g_pLTDatabase->GetInt32(hAttr, vi, 0) << "\n"; break;
						case eAttributeType_Float:
							recIni << key << "=" << g_pLTDatabase->GetFloat(hAttr, vi, 0.0f) << "\n"; break;
						case eAttributeType_Bool:
							recIni << key << "=" << (g_pLTDatabase->GetBool(hAttr, vi, false) ? "True" : "False") << "\n"; break;
						case eAttributeType_String:
							recIni << key << "=" << g_pLTDatabase->GetString(hAttr, vi, "") << "\n"; break;
						case eAttributeType_WString:
						{
							const wchar_t* ws = g_pLTDatabase->GetWString(hAttr, vi, L"");

							recIni << key << "=" << ws << "\n";
							break;
						}
						case eAttributeType_Vector2: {
							LTVector2 v = g_pLTDatabase->GetVector2(hAttr, vi, LTVector2{});
							recIni << key << "=" << v.x << "," << v.y << "\n"; break;
						}
						case eAttributeType_Vector3: {
							LTVector v = g_pLTDatabase->GetVector3(hAttr, vi, LTVector{});
							recIni << key << "=" << v.x << "," << v.y << "," << v.z << "\n"; break;
						}
						case eAttributeType_Vector4: {
							LTVector4 v = g_pLTDatabase->GetVector4(hAttr, vi, LTVector4{});
							recIni << key << "=" << v.x << "," << v.y << "," << v.z << "," << v.w << "\n"; break;
						}
						case eAttributeType_RecordLink: {
							HRECORD hLink = g_pLTDatabase->GetRecordLink(hAttr, vi, nullptr);
							if (hLink) {
								HCATEGORY hLinkCat = g_pLTDatabase->GetRecordParent(hLink);
								std::string linkCat = g_pLTDatabase->GetCategoryName(hLinkCat);
								std::string linkRec = g_pLTDatabase->GetRecordName(hLink);
								recIni << key << "=" << linkCat << "/" << linkRec << "\n";
							}
							else recIni << key << "=<none>\n";
							break;
						}
						default:
							recIni << key << "=<unhandled>\n"; break;
						}
					}
				}
				else {

					std::ostringstream sectionName;
					sectionName << "Attrib";
					for (size_t t = 0; t < toks.size(); ++t) {
						sectionName << "." << toks[t];

						if (IsAllDigits(toks[t])) continue;
						if (t + 1 < toks.size() && IsAllDigits(toks[t + 1])) {
							sectionName << ".0." << toks[t + 1];
							++t;
						}
						else {
							sectionName << ".0";
						}
					}

					recIni << "\n[" << sectionName.str() << "]\n";
					recIni << "Inherit=False\nPlaceHolder=False\nTodo=False\nModified=False\nOverride=False\n";
					recIni << "Lock=\nComment=\n";

					for (uint32 vi = 0; vi < numVals; ++vi) {
						char key[32]; sprintf(key, "Value.%04u", vi);
						switch (type) {
						case eAttributeType_Int32:
							recIni << key << "=" << g_pLTDatabase->GetInt32(hAttr, vi, 0) << "\n"; break;
						case eAttributeType_Float:
							recIni << key << "=" << g_pLTDatabase->GetFloat(hAttr, vi, 0.0f) << "\n"; break;
						case eAttributeType_Bool:
							recIni << key << "=" << (g_pLTDatabase->GetBool(hAttr, vi, false) ? "True" : "False") << "\n"; break;
						case eAttributeType_String:
							recIni << key << "=" << g_pLTDatabase->GetString(hAttr, vi, "") << "\n"; break;
						case eAttributeType_WString:
						{
							const wchar_t* ws = g_pLTDatabase->GetWString(hAttr, vi, L"");

							recIni << key << "=" << ws << "\n";
							break;
						}
						case eAttributeType_Vector2: {
							LTVector2 v = g_pLTDatabase->GetVector2(hAttr, vi, LTVector2{});
							recIni << key << "=" << v.x << "," << v.y << "\n"; break;
						}
						case eAttributeType_Vector3: {
							LTVector v = g_pLTDatabase->GetVector3(hAttr, vi, LTVector{});
							recIni << key << "=" << v.x << "," << v.y << "," << v.z << "\n"; break;
						}
						case eAttributeType_Vector4: {
							LTVector4 v = g_pLTDatabase->GetVector4(hAttr, vi, LTVector4{});
							recIni << key << "=" << v.x << "," << v.y << "," << v.z << "," << v.w << "\n"; break;
						}
						case eAttributeType_RecordLink: {
							HRECORD hLink = g_pLTDatabase->GetRecordLink(hAttr, vi, nullptr);
							if (hLink) {
								HCATEGORY hLinkCat = g_pLTDatabase->GetRecordParent(hLink);
								std::string linkCat = g_pLTDatabase->GetCategoryName(hLinkCat);
								std::string linkRec = g_pLTDatabase->GetRecordName(hLink);
								recIni << key << "=" << linkCat << "/" << linkRec << "\n";
							}
							else recIni << key << "=<none>\n";
							break;
						}
						default:
							recIni << key << "=<unhandled>\n"; break;
						}
					}
				}
			}

			WriteIniFile((catPath / (recName + ".record")).string(), recIni.str());
		}

		std::unordered_set<std::string> allTopTokens;
		for (const auto& a : attrInfos) {
			if (!a.toks.empty()) allTopTokens.insert(a.toks[0]);
		}

		std::unordered_set<std::string> generatedStructs;

		std::function<void(const std::vector<std::string>&)> BuildStruct;
		BuildStruct = [&](const std::vector<std::string>& prefixTokens) {

			if (prefixTokens.empty()) return;
			std::string structName = prefixTokens.back();
			if (generatedStructs.count(structName)) return;

			struct ChildInfo {
				bool hasNested = false;
				EAttributeType sampleType = eAttributeType_Invalid;
				uint32 maxVals = 0;
				std::string linkedSchema;
				bool haveSampleType = false;
			};
			std::unordered_map<std::string, ChildInfo> children;

			for (const auto& ai : attrInfos) {

				size_t posAfter = 0;
				if (!MatchPrefix(ai.toks, prefixTokens, posAfter)) continue;

				size_t childIdx = FindNextNonNumeric(ai.toks, posAfter);
				if (childIdx == std::string::npos) continue;

				const std::string& childName = ai.toks[childIdx];

				size_t afterChild = FindNextNonNumeric(ai.toks, childIdx + 1);
				bool childHasFurther = (afterChild != std::string::npos);

				auto& ci = children[childName];
				ci.hasNested = ci.hasNested || childHasFurther;

				int lastNonNum = -1;
				for (int t = (int)ai.toks.size() - 1; t >= 0; --t) {
					if (!IsAllDigits(ai.toks[t])) { lastNonNum = t; break; }
				}
				if (lastNonNum >= 0 && ai.toks[lastNonNum] == childName) {

					if (!ci.haveSampleType) {
						ci.sampleType = ai.type;
						ci.haveSampleType = true;
					}
					ci.maxVals = std::max(ci.maxVals, ai.numVals);

					if (ai.type == eAttributeType_RecordLink && ai.numVals > 0) {
						HRECORD hLink = g_pLTDatabase->GetRecordLink(ai.hAttr, 0, nullptr);
						if (hLink) {
							HCATEGORY hLinkCat = g_pLTDatabase->GetRecordParent(hLink);
							if (hLinkCat) {
								std::string linkCat = g_pLTDatabase->GetCategoryName(hLinkCat);
								std::replace(linkCat.begin(), linkCat.end(), '/', '.');
								ci.linkedSchema = linkCat;
							}
						}
					}
				}
			}

			if (children.empty()) {

				generatedStructs.insert(structName);
				return;
			}

			std::ostringstream structIni;

			for (const auto& kv : children) {
				const std::string& childName = kv.first;
				const ChildInfo& ci = kv.second;

				structIni << "[Attrib." << childName << "]\n";
				if (ci.hasNested) {
					structIni << "Type=Struct\n";
					structIni << "Data=" << childName << ".struct\n";
					structIni << "Default=\n";
					structIni << "Unicode=False\n";
					structIni << "Deleted=False\n";
					structIni << "Values=-1\n";
					structIni << "Help=\n\n";
				}
				else {

					switch (ci.sampleType) {
					case eAttributeType_Int32:
						structIni << "Type=Integer\n";
						structIni << "Default=0\n";
						structIni << "Unicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					case eAttributeType_Float:
						structIni << "Type=Float\n";
						structIni << "Default=0.0\n";
						structIni << "Unicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					case eAttributeType_Bool:
						structIni << "Type=Boolean\n";
						structIni << "Default=False\n";
						structIni << "Unicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					case eAttributeType_String:
						structIni << "Type=Text\n";
						structIni << "Default=\nUnicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					case eAttributeType_WString:
						structIni << "Type=Text\n";
						structIni << "Default=\nUnicode=True\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					case eAttributeType_Vector2:
						structIni << "Type=Vector2\n";
						structIni << "Default=\nUnicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					case eAttributeType_Vector3:
						structIni << "Type=Vector3\n";
						structIni << "Default=\nUnicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					case eAttributeType_Vector4:
						structIni << "Type=Vector4\n";
						structIni << "Default=\nUnicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					case eAttributeType_RecordLink:
						structIni << "Type=RecordLink\n";
						structIni << "Default=<none>\n";
						structIni << "Data=" << (ci.linkedSchema.empty() ? "" : ci.linkedSchema) << "\n";
						structIni << "Unicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=-1\nHelp=\n\n";
						break;
					default:
						structIni << "Type=Unknown\n";
						structIni << "Default=\nUnicode=False\nDeleted=False\nInherit=True\n";
						structIni << "Values=" << (ci.maxVals ? ci.maxVals : 1) << "\nHelp=\n\n";
						break;
					}
				}
			}

			std::filesystem::path structPath = catPath / (structName + ".struct");
			WriteIniFile(structPath.string(), structIni.str());
			generatedStructs.insert(structName);

			for (const auto& kv : children) {
				if (kv.second.hasNested) {

					std::vector<std::string> childPrefix = prefixTokens;
					childPrefix.push_back(kv.first);
					BuildStruct(childPrefix);
				}
			}
			};

		for (const auto& top : allTopTokens) {

			bool hasNested = false;
			for (const auto& ai : attrInfos) {
				if (!ai.toks.empty() && ai.toks[0] == top && ai.toks.size() > 1) { hasNested = true; break; }
			}

			bool topIsStruct = false;
			for (const auto& ai : attrInfos) {
				if (ai.toks.size() == 1 && ai.toks[0] == top && ai.type == eAttributeType_Struct) { topIsStruct = true; break; }
			}
			if (hasNested || topIsStruct) {
				std::vector<std::string> pfx = { top };
				BuildStruct(pfx);
			}
		}

		WriteIniFile((catPath / (lastPart + ".schema")).string(), schemaIni.str());
	}
}

int main(int argc, char* argv[])
{
	const char* dbFilePath = "TheRaw.Gamdb00p";
	if (argc >= 2) {
		dbFilePath = argv[1];
	}


	SetConsoleTitleW(L".Gamdb00p dumper by LeftSpace\n");

	s_hDatabaseInst = LTLibraryLoader::OpenLibrary("GameDatabase.dll");
	if (s_hDatabaseInst)
	{

		HLTPROC hProc = LTLibraryLoader::GetProcAddress(s_hDatabaseInst, "GetIDatabaseMgr");
		LTASSERT(hProc != NULL, "Unable to retrieve the DatabaseMgr function!");
		if (hProc)
		{
			TGetIDatabaseMgrFn DBfn = (TGetIDatabaseMgrFn)hProc;
			g_pLTDatabase = DBfn();
		}

		hProc = LTLibraryLoader::GetProcAddress(s_hDatabaseInst, "GetIDatabaseCreatorMgr");
		LTASSERT(hProc != NULL, "Unable to retrieve the DatabaseCreatorMgr function!");
		if (hProc)
		{
			TGetIDatabaseCreatorMgrFn DBfn = (TGetIDatabaseCreatorMgrFn)hProc;
			g_pLTDatabaseCreator = DBfn();
		}
	}
	else
	{
		printf("Failed to load GameDatabase.dll\n");
		std::getchar();
		return -1;
	}

	auto hDB = OpenGameDatabaseFile(dbFilePath);
	if (hDB) {
		std::printf("hDB %p\n", hDB);
		DumpDatabaseRecursive(hDB, ".\\Output\\");
		std::printf("Dump OK\n");
	}
	else
	{
		std::printf("Failed to open database\n");
	}

	std::getchar();
}