#pragma once

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif


/**
 * \ingroup CommonClasses
 * \return the number of adapters currently active on the system with a valid ip address (other than localhost)
 */
int GetNumberOfIPAdapters();
/**
 * \ingroup CommonClasses
 * returns the ip number of the given adapter. if the param is set to 0 (or ommited) the first found ip address is returned.
 * if the adapternumber is higher than the available ip adapters, an empty string is returned.
 * \remark on most systems only one adapter with a valid ip address is available. So calling this function without parameter
 * will return the local ip address.
 * \return the ip address
 */
CString GetIPNumber(int adapternumber = 0);
/**
 * \ingroup CommonClasses
 * returns the ip netmask of the given adapter.  if the param is set to 0 (or ommited) the first found ip netmask is returned.
 * if the adapternumber is higher than the available ip adapters, an empty string is returned.
 * \remark on most systems only one adapter with a valid ip address is available. So calling this function without parameter
 * will return the local ip netmask.
 * \return the ip netmask
 */
CString GetIPMask(int adapternumber = 0);
/**
 * \ingroup CommonClasses
 * returns the ip gateway of the given adapter.  if the param is set to 0 (or ommited) the first found ip gateway is returned.
 * if the adapternumber is higher than the available ip adapters, an empty string is returned.
 * \remark on most systems only one adapter with a valid ip address is available. So calling this function without parameter
 * will return the local ip gateway.
 * \return the ip gateway
 */
CString GetIPGateway(int adapternumber = 0);
/**
 * \ingroup CommonClasses
 * returns the mac address of the given adapter in the format "xx xx xx xx xx xx".  if the param is set to 0 (or ommited) the first found mac address is returned.
 * if the adapternumber is higher than the available ip adapters, an empty string is returned.
 * \remark on most systems only one adapter with a valid ip address is available. So calling this function without parameter
 * will return the local mac address.
 * \return the mac address
 */
CString GetMACAddress(int adapternumber = 0);

/**
 * \ingroup CommonClasses
 * Converts a string containing an ip address to an S_addr. If the string is already a valid ip then it's simply converted. If the string is a 
 * domain name then it is looked up via DNS and then converted.
 * \param ipstring an ip address in the form "a.b.c.d" or "www.domain.com"
 * \return the converted ip address or NULL if an error occured. Use WSAGetLastError() for error information.
 */   
ULONG GetIPFromString(LPCTSTR ipstring);

