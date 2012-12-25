// TextParsing.cpp
//
// Copyright (c) 1995-1998 - Richard Langlois and Grokksoft Inc.
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which
// you have taken this file. If you can not find the license you can not use
// this file.
//
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions
// and limitations under the License.
//

#include "StdAfx.h"

#include "TextParsing.h"

namespace HoverRace {
namespace ResourceCompiler {

// Local data
static CMapStringToString gDefineMap;

BOOL MR_ReadPredefinedConstants(const Util::OS::path_t &pFileName)
{
	BOOL lReturnValue = TRUE;
#	ifdef _WIN32
		FILE *lFile = _wfopen(pFileName.file_string().c_str(), L"r");
#	else
		FILE *lFile = fopen(pFileName.file_string().c_str(), "r");
#	endif

	if(lFile == NULL) {
		lReturnValue = FALSE;

		fprintf(stderr, "%s: %s.\n", _("ERROR"), _("unable to open defines file"));
	}
	else {
		char lBuffer[250];
		int lLineNo = 1;
		while(lReturnValue && fgets(lBuffer, sizeof(lBuffer), lFile)) {
			CString lLine = MR_PreProcLine(lBuffer);
			lLineNo++;

			if(MR_BeginByKeyword(lLine, "#define")) {
				char lKey[100];
				char lValue[100];

				if(sscanf(lLine, " #define %s %s ", lKey, &lValue) != 2) {
					lReturnValue = FALSE;

					fprintf(stderr, "%s: %s, %s %d.\n", _("ERROR"), _("syntax error in defines file"), _("line"), lLineNo);
				}
				else {
					// add the define to the list
					gDefineMap.SetAt(lKey, lValue);
				}
			}
		}

		fclose(lFile);
	}

	return lReturnValue;
}

CString MR_PreProcLine(const char *pLine)
{
	// scan the line and seardh for predefined keyword
	CString lReturnValue;

	if(pLine != NULL) {
		const char *lPtr = pLine;
		BOOL lEnd = FALSE;
		const char *lTokenStart = NULL;

		while(!lEnd) {
			switch (*lPtr) {
				case 0:
					lEnd = TRUE;
				case ' ':
				case ',':
				case '\n':
				case '\r':
				case '\t':

					if(lTokenStart) {
						CString lValue;

						CString lKey(lTokenStart, lPtr - lTokenStart);

						lTokenStart = NULL;

						if(gDefineMap.Lookup(lKey, lValue)) {
							lReturnValue += lValue;
						}
						else {
							lReturnValue += lKey;
						}

					}

					if(!lEnd) {
						lReturnValue += *lPtr;
					}

					break;

				default:
					if(!lTokenStart) {
						lTokenStart = lPtr;
					}
					break;
			}

			lPtr++;
		}
	}
	return lReturnValue;
}

BOOL MR_BeginByKeyword(const char *pLine, const char *pKeyword)
{
	BOOL lReturnValue = FALSE;

	if(!strncmp(MR_SkipLeadingSpaces(pLine), pKeyword, strlen(pKeyword))) {
		lReturnValue = TRUE;
	}

	return lReturnValue;
}

int MR_ContainsKeyword(const char *pLine, const char **pKeywordList)
{
	int lReturnValue = 0;

	pLine = MR_SkipLeadingSpaces(pLine);

	while(pKeywordList[lReturnValue] != NULL) {
		if(!strncmp(pLine, pKeywordList[lReturnValue], strlen(pKeywordList[lReturnValue]))) {
			return lReturnValue;
		}
		lReturnValue++;
	}
	return -1;
}

const char *MR_SkipLeadingSpaces(const char *pString)
{
	const char *lReturnValue = pString;

	while(*lReturnValue == ' ' || *lReturnValue == '\t') {
		lReturnValue++;
	}

	return lReturnValue;
}

}  // namespace ResourceCompiler
}  // namespace HoverRace