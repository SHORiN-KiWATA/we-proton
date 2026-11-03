/* Synthetic route API performance probe. Run only with --isolated.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Creates one test adapter, no session, and sends no network packets.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LARGE_INTEGER frequency;
static double now_ms(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart*1000/frequency.QuadPart; }
static int prepare_routes(DWORD index)
{
    FILE *f=fopen("Z:\\test\\output\\ready","wb"); double end=now_ms()+30000;
    if(!f)return 0;
    fprintf(f,"%lu\n",index); fclose(f);
    do {
        f=fopen("Z:\\test\\output\\configured","rb");
        if(f){fclose(f);return 1;}
        Sleep(10);
    }while(now_ms()<end);
    return 0;
}

static DWORD matching_rows(const MIB_IPFORWARD_TABLE2 *table,DWORD index)
{
    DWORD i,count=0;
    for(i=0;i<table->NumEntries;i++)
    {
        const MIB_IPFORWARD_ROW2 *r=table->Table+i;
        if(r->InterfaceIndex==index && r->DestinationPrefix.Prefix.si_family==AF_INET &&
           (ntohl(r->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr)&0xfffe0000u)==0xc6120000u) count++;
    }
    return count;
}

int main(int argc,char **argv)
{
    void *(WINAPI *create_adapter)(LPCWSTR,LPCWSTR,const GUID *);
    void (WINAPI *close_adapter)(void *);
    void (WINAPI *get_luid)(void *,LUID *);
    HMODULE lib; void *adapter; NET_LUID luid={0}; DWORD index=0,error,count,i,failures=0;
    MIB_IPFORWARD_TABLE2 *table=NULL;
    MIB_IPFORWARDTABLE *old_table=NULL; MIB_IPINTERFACE_ROW interface_row;
    ULONG bytes=0; double start,batch,last,total;
    if(argc!=4 || strcmp(argv[1],"--isolated") || strcmp(argv[2],"--routes"))return 3;
    count=(DWORD)strtoul(argv[3],NULL,10);
    if(count<1 || count>5000)return 3;
    setvbuf(stdout,NULL,_IONBF,0); QueryPerformanceFrequency(&frequency);
    lib=LoadLibraryW(L"wintun.dll"); if(!lib){printf("ERROR load=%lu\n",GetLastError());return 3;}
    create_adapter=(void*)GetProcAddress(lib,"WintunCreateAdapter");
    close_adapter=(void*)GetProcAddress(lib,"WintunCloseAdapter");
    get_luid=(void*)GetProcAddress(lib,"WintunGetAdapterLUID");
    if(!create_adapter || !close_adapter || !get_luid)return 3;
    adapter=create_adapter(L"RouteProbe",L"RoutePerf",NULL);
    if(!adapter){printf("ERROR create=%lu\n",GetLastError());return 3;}
    get_luid(adapter,(LUID*)&luid);
    error=ConvertInterfaceLuidToIndex(&luid,&index);
    printf("READY count=%lu index=%lu luid=%llx error=%lu\n",count,index,(unsigned long long)luid.Value,error);
    if(error || !index || !prepare_routes(index)){close_adapter(adapter);return 3;}
    for(i=0;i<6;i++)
    {
        start=now_ms(); error=GetIpForwardTable2(AF_INET,&table); total=now_ms()-start;
        if(error){printf("ERROR table2=%lu\n",error);failures++;break;}
        printf("ENUM iteration=%lu milliseconds=%.3f matching=%lu total=%lu warmup=%d\n",i,total,matching_rows(table,index),table->NumEntries,i==0);
        if(matching_rows(table,index)!=count)failures++;
        FreeMibTable(table);table=NULL;
    }
    start=now_ms(); error=GetIpForwardTable(NULL,&bytes,FALSE);
    if(error==ERROR_INSUFFICIENT_BUFFER && (old_table=malloc(bytes)))error=GetIpForwardTable(old_table,&bytes,FALSE);
    printf("OLD_ENUM milliseconds=%.3f error=%lu total=%lu\n",now_ms()-start,error,old_table&&!error?old_table->dwNumEntries:0);
    if(error)failures++;
    free(old_table);
    if(failures){close_adapter(adapter);printf("RESULT failures=%lu\n",failures);return 3;}
    start=last=now_ms();
    for(i=0;i<count;i++)
    {
        MIB_IPFORWARDROW row={0};
        row.dwForwardDest=htonl(0xc6120000u+i);row.dwForwardMask=0xffffffffu;
        row.dwForwardIfIndex=index;row.dwForwardType=MIB_IPROUTE_TYPE_DIRECT;
        row.dwForwardProto=MIB_IPPROTO_NETMGMT;row.dwForwardMetric1=1;
        error=DeleteIpForwardEntry(&row);
        if(error){if(failures<8)printf("DELETE_ERROR at=%lu error=%lu\n",i,error);failures++;}
        if((i+1)%250==0 || i+1==count)
        {
            batch=now_ms();printf("DELETE_PROGRESS deleted=%lu elapsed_ms=%.3f batch_ms=%.3f failures=%lu\n",i+1,batch-start,batch-last,failures);last=batch;
        }
    }
    total=now_ms()-start;
    error=GetIpForwardTable2(AF_INET,&table);
    printf("DELETE_TOTAL count=%lu milliseconds=%.3f failures=%lu\n",count,total,failures);
    printf("FINAL error=%lu matching=%lu\n",error,error?~0u:matching_rows(table,index));
    if(error || matching_rows(table,index))failures++;
    if(table)FreeMibTable(table);
    table=NULL;
    InitializeIpInterfaceEntry(&interface_row);
    interface_row.Family=AF_INET;interface_row.InterfaceLuid=luid;interface_row.InterfaceIndex=index;
    error=GetIpInterfaceEntry(&interface_row);
    printf("INTERFACE_METRIC error=%lu metric=%lu\n",error,interface_row.Metric);
    if(error || interface_row.Metric>=0x7ffffffeu){close_adapter(adapter);return 3;}
    start=last=now_ms();
    for(i=0;i<count;i++)
    {
        MIB_IPFORWARDROW row={0};
        row.dwForwardDest=htonl(0xc6120000u+i);row.dwForwardMask=0xffffffffu;
        row.dwForwardIfIndex=index;row.dwForwardType=MIB_IPROUTE_TYPE_DIRECT;
        row.dwForwardProto=MIB_IPPROTO_NETMGMT;row.dwForwardMetric1=interface_row.Metric+1;
        error=CreateIpForwardEntry(&row);
        if(error){if(failures<8)printf("CREATE_ERROR at=%lu error=%lu\n",i,error);failures++;}
        if((i+1)%250==0 || i+1==count)
        {
            batch=now_ms();printf("CREATE_PROGRESS created=%lu elapsed_ms=%.3f batch_ms=%.3f failures=%lu\n",i+1,batch-start,batch-last,failures);last=batch;
        }
    }
    total=now_ms()-start;
    error=GetIpForwardTable2(AF_INET,&table);
    printf("CREATE_TOTAL count=%lu milliseconds=%.3f failures=%lu\n",count,total,failures);
    printf("CREATED_FINAL error=%lu matching=%lu\n",error,error?~0u:matching_rows(table,index));
    if(error || matching_rows(table,index)!=count)failures++;
    if(table)FreeMibTable(table);
    close_adapter(adapter);FreeLibrary(lib);
    printf("RESULT failures=%lu\n",failures);
    return failures?1:0;
}
