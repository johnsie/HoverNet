#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include <atlstr.h>

#pragma comment(lib, "wininet.lib")

static BOOL FetchHTTPSContent(const char* pHost, const char* pPath, CString& pBuffer)
{
   HINTERNET hInternet = NULL;
   HINTERNET hConnection = NULL;
   HINTERNET hRequest = NULL;
   BOOL lReturnValue = FALSE;

   // Validate inputs
   if (!pHost || !pPath || !pHost[0] || !pPath[0]) {
      printf("FetchHTTPSContent: NULL or empty host/path!\n");
      return FALSE;
   }

   printf("FetchHTTPSContent START: Host=%s, Path=%s\n", pHost, pPath);

   try {
      hInternet = InternetOpen("HoverNet/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
      if (!hInternet) {
         printf("ERROR: InternetOpen failed\n");
         return FALSE;
      }
      printf("InternetOpen: OK\n");

      hConnection = InternetConnect(
         hInternet,
         pHost,
         INTERNET_DEFAULT_HTTPS_PORT,
         NULL,
         NULL,
         INTERNET_SERVICE_HTTP,
         0,
         0
      );

      if (!hConnection) {
         printf("ERROR: InternetConnect failed, error=%lu\n", GetLastError());
         InternetCloseHandle(hInternet);
         return FALSE;
      }
      printf("InternetConnect: OK\n");

      DWORD dwFlags = INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE;
      hRequest = HttpOpenRequest(
         hConnection,
         "GET",
         pPath,
         NULL,
         NULL,
         NULL,
         dwFlags,
         0
      );

      if (!hRequest) {
         printf("ERROR: HttpOpenRequest failed, error=%lu\n", GetLastError());
         InternetCloseHandle(hConnection);
         InternetCloseHandle(hInternet);
         return FALSE;
      }
      printf("HttpOpenRequest: OK\n");

      // Ignore SSL certificate errors for self-signed certs
      DWORD dwFlags2 = SECURITY_FLAG_IGNORE_UNKNOWN_CA | 
                       SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                       SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
      InternetSetOption(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &dwFlags2, sizeof(dwFlags2));
      printf("SSL flags set\n");

      if (!HttpSendRequest(hRequest, NULL, 0, NULL, 0)) {
         printf("ERROR: HttpSendRequest failed, Error=%lu\n", GetLastError());
         InternetCloseHandle(hRequest);
         InternetCloseHandle(hConnection);
         InternetCloseHandle(hInternet);
         return FALSE;
      }
      printf("HttpSendRequest: OK\n");

      // Query the response status
      DWORD dwStatus = 0;
      DWORD dwStatusLen = sizeof(dwStatus);
      if (HttpQueryInfo(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, 
                       &dwStatus, &dwStatusLen, NULL)) {
         printf("HTTP Status = %lu\n", dwStatus);
      } else {
         printf("HttpQueryInfo failed, error=%lu\n", GetLastError());
      }

      pBuffer.Empty();
      DWORD dwSize = 0;
      BYTE szBuffer[4096];

      while (InternetReadFile(hRequest, szBuffer, sizeof(szBuffer), &dwSize)) {
         if (dwSize == 0) break;
         pBuffer.Append((const char*)szBuffer, dwSize);
      }
      printf("InternetReadFile: Total bytes read = %d\n", pBuffer.GetLength());

      if (!pBuffer.IsEmpty()) {
         printf("SUCCESS: BufferLen=%d\n", pBuffer.GetLength());
         printf("Response:\n%s\n", (const char*)pBuffer);
         lReturnValue = TRUE;
      }
   }
   catch (...) {
      printf("EXCEPTION in FetchHTTPSContent\n");
   }

   // Cleanup
   if (hRequest) InternetCloseHandle(hRequest);
   if (hConnection) InternetCloseHandle(hConnection);
   if (hInternet) InternetCloseHandle(hInternet);

   return lReturnValue;
}

int main()
{
   printf("=== HTTPS Fetch Test ===\n");
   
   CString buffer;
   BOOL success = FetchHTTPSContent("steeky.com", "/hover/roomlist2.txt", buffer);
   
   printf("\n=== Result ===\n");
   printf("Success: %d\n", success);
   printf("Buffer Length: %d\n", buffer.GetLength());
   
   if (!buffer.IsEmpty()) {
      printf("\n=== Full Response ===\n");
      printf("%s\n", (const char*)buffer);
   }
   
   return 0;
}
