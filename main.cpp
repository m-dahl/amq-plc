#include <stdio.h>
#include <stdlib.h>
#include "snap7.h"
#include "amq.h"
#include "JSON.h"
#include "JSONValue.h"

TS7Client *plc;

void print_usage()
{
    wprintf(L"Usage\n");
    wprintf(L"  client <IP> [Rack=0 Slot=2]\n");
    wprintf(L"Example\n");
    wprintf(L"  client 192.168.1.101 0 2\n");
    wprintf(L"or\n");
    wprintf(L"  client 192.168.1.101\n");
    getchar();
}

bool ok_or_error(int res, const std::wstring& caller, std::wstring& error) {
  if(res != 0) {
    error = caller;
    if(res < 0) {
      error += L": snap7 library error (-1)";
    } else {
      error += L": ";
      error += CliErrorText(res);
    }
  }
  return res==0;
}

void print_plc_info() {
  TS7OrderCode oc;
  int res=plc->GetOrderCode(&oc);
  if(!res) {
    wprintf(L"  Order Code : %s\n",oc.Code);
    wprintf(L"  Version    : %d.%d.%d\n",oc.V1,oc.V2,oc.V3);
  };
  TS7CpuInfo cpui;
  res=plc->GetCpuInfo(&cpui);
  if(!res) {
    wprintf(L"  Module Type Name : %s\n",cpui.ModuleTypeName);
    wprintf(L"  Serial Number    : %s\n",cpui.SerialNumber);
    wprintf(L"  AS Name          : %s\n",cpui.ASName);
    wprintf(L"  Module Name      : %s\n",cpui.ModuleName);
  };
  TS7CpInfo cpi;
  res=plc->GetCpInfo(&cpi);
  if(!res) {
    wprintf(L"  Max PDU Length   : %d bytes\n",cpi.MaxPduLengt);
    wprintf(L"  Max Connections  : %d \n",cpi.MaxConnections);
    wprintf(L"  Max MPI Rate     : %d bps\n",cpi.MaxMpiRate);
    wprintf(L"  Max Bus Rate     : %d bps\n",cpi.MaxBusRate);
  };
  int Status=plc->PlcStatus();
  switch (Status) {
  case S7CpuStatusRun : wprintf(L"  RUN\n"); break;
  case S7CpuStatusStop: wprintf(L"  STOP\n"); break;
  default             : wprintf(L"  UNKNOWN\n"); break;
  }
}

bool plc_connect(const char* address, int rack, int slot) {
  int res = plc->ConnectTo(address,rack,slot);
  std::wstring err; ;
  if(ok_or_error(res, L"Failed to connect", err)) {
    wprintf(L"  Connected to   : %s (rack=%d, slot=%d)\n",address,rack,slot);
    wprintf(L"  PDU Requested  : %d bytes\n",plc->PDURequested());
    wprintf(L"  PDU Negotiated : %d bytes\n",plc->PDULength());
  } else {
    wprintf(L"%S\n", err.c_str());
  }
  return res==0;
}

void plc_disconnect() {
  plc->Disconnect();
}

#define PLC_TYPE_NOT_IMPLEMENTED 0
#define PLC_TYPE_BIT 1
#define PLC_TYPE_INT8 2
#define PLC_TYPE_INT16 3

int type_from_str(const std::wstring& str) {
  if(str == std::wstring(L"bool")) return PLC_TYPE_BIT;
  if(str == std::wstring(L"byte")) return PLC_TYPE_INT8;
  if(str == std::wstring(L"int")) return PLC_TYPE_INT16;
  return PLC_TYPE_NOT_IMPLEMENTED;
}

std::wstring str_from_type(int type) {
  if(type == PLC_TYPE_BIT) return std::wstring(L"bool");
  if(type == PLC_TYPE_INT8) return std::wstring(L"byte");
  if(type == PLC_TYPE_INT16) return std::wstring(L"int");
  return std::wstring(L"unknown");
}

struct cmd { std::wstring id; int type; int value;
  int db; int byt; int bit; };

typedef std::map<std::wstring,cmd>::iterator sub_iter;
std::map<std::wstring,cmd> subscribe_map;

void poll_func() {
  static std::wstring last_json;
  // read data and send it out
  JSONArray jsonarr;
  for(sub_iter i = subscribe_map.begin(); i!=subscribe_map.end(); ++i) {
    cmd& c = i->second;

    if(c.type == PLC_TYPE_BIT) {
      byte data = 0;
      int res = plc->DBRead(c.db, c.byt, 1, &data);
      c.value = data & (1 << c.bit);
    }
    else if(c.type == PLC_TYPE_INT8) {
      byte data = 0;
      int res = plc->DBRead(c.db, c.byt, 1, &data);
      c.value = data;
    }
    else if(c.type == PLC_TYPE_INT16) {
      word data = 0;
      int res = plc->DBRead(c.db, c.byt, 2, &data);
      c.value = (signed short int)(((data >> 8) & 0xFF) | ((data << 8) & 0xFF00));
    }

    JSONObject db;
    db[L"id"] = new JSONValue(c.id);
    JSONObject address;
    address[L"db"] = new JSONValue((double)c.db);
    address[L"byte"] = new JSONValue((double)c.byt);
    address[L"bit"] = new JSONValue((double)c.bit);
    db[L"address"] = new JSONValue(address);
    db[L"valueType"] = new JSONValue(str_from_type(c.type));
    if(c.type == PLC_TYPE_BIT) {
      db[L"value"] = new JSONValue((bool)c.value!=0);
    } else {
      db[L"value"] = new JSONValue((double)c.value);
    }
    jsonarr.push_back(new JSONValue(db));
  }

  JSONObject reply;
  reply[L"dbs"] = new JSONValue(jsonarr);

	JSONValue *json = new JSONValue(reply);
  std::wstring json_str = json->Stringify(true);
  if (last_json != json_str) {
    wprintf(L"Sending new subscribtion state:\n%S\n",json_str.c_str());
    amq_send(json_str);
    last_json = json_str;
  }
  delete json;
}

int write(JSONValue *main_object) {
  JSONValue *dbs = main_object->Child(L"dbs");
  if(dbs && dbs->IsArray()) {
    JSONArray a = dbs->AsArray();
    JSONArray::const_iterator iter;
    for(iter = a.begin();iter != a.end();++iter)
    {
      cmd c;

      JSONValue *id = (*iter)->Child(L"id");
      if(id) {
        c.id = id->AsString();
      } else goto error;

      JSONValue *address = (*iter)->Child(L"address");
      if(address) {
        JSONValue *db = address->Child(L"db");
        JSONValue *byt = address->Child(L"byte");
        JSONValue *bit = address->Child(L"bit");
        if(db && byt && bit) {
          c.db = (int)db->AsNumber();
          c.byt = (int)byt->AsNumber();
          c.bit = (int)bit->AsNumber();
        } else goto error;
      } else goto error;

      JSONValue *type = (*iter)->Child(L"valueType");
      if(type) {
        c.type = type_from_str(type->AsString());
      } else goto error;
      JSONValue *value = (*iter)->Child(L"value");
      if(value) {
        if(c.type == PLC_TYPE_BIT) c.value = (int)value->AsBool()?1:0;
        else if(c.type == PLC_TYPE_INT8) c.value = (int)value->AsNumber();
        else if(c.type == PLC_TYPE_INT16) c.value = (int)value->AsNumber();
      } else goto error;

      if(c.type == PLC_TYPE_BIT) {
        byte data = 0;
        int res = plc->DBRead(c.db, c.byt, 1, &data);
        if(c.value) data = data | (1 << c.bit);
        else data = data & ~(1 << c.bit);
        res = plc->DBWrite(c.db, c.byt, 1, &data);
      }
      else if(c.type == PLC_TYPE_INT8) {
        byte data = (byte)c.value;
        int res = plc->DBWrite(c.db, c.byt, 1, &data);
      }
      else if(c.type == PLC_TYPE_INT16) {
        word data = ((c.value >> 8) & 0xFF) | ((c.value << 8) & 0xFF00);
        int res = plc->DBWrite(c.db, c.byt, 2, &data);
      }
      else if(c.type == PLC_TYPE_NOT_IMPLEMENTED) {
        goto error;
      }
    }
  } else goto error;
  return 1;
error:
  return 0;
}

int subscribe(JSONValue *main_object) {
  JSONValue *dbs = main_object->Child(L"dbs");
  if(dbs && dbs->IsArray()) {
    JSONArray a = dbs->AsArray();
    JSONArray::const_iterator iter;
    for(iter = a.begin();iter != a.end();++iter)
    {
      cmd c;

      JSONValue *id = (*iter)->Child(L"id");
      if(id) {
        c.id = id->AsString();
      } else goto error;

      JSONValue *address = (*iter)->Child(L"address");
      if(address) {
        JSONValue *db = address->Child(L"db");
        JSONValue *byt = address->Child(L"byte");
        JSONValue *bit = address->Child(L"bit");
        if(db && byt && bit) {
          c.db = (int)db->AsNumber();
          c.byt = (int)byt->AsNumber();
          c.bit = (int)bit->AsNumber();
        } else goto error;
      } else goto error;

      JSONValue *type = (*iter)->Child(L"valueType");
      if(type) {
        c.type = type_from_str(type->AsString());
      } else goto error;

      subscribe_map[c.id] = c;
    }
  } else goto error;
  wprintf(L"Currently subscribed to %d addresses.\n", subscribe_map.size());

  return 1;
error:
  return 0;
}

void msg_func(const std::wstring& text) {
  JSONValue *main_object = JSON::Parse(text.c_str());
  if(main_object) {
    // Print the main object.
    wprintf(L"New request:\n");
    wprintf(main_object->Stringify(true).c_str());
    wprintf(L"\n");

    JSONValue *command = main_object->Child(L"command");
    if(command && command->IsString()) {
      std::wstring cmd = command->AsString();
      if(cmd == std::wstring(L"subscribe")) {
        // add dbs to subscribe list
        if(!subscribe(main_object)) {
          wprintf(L"Error subscribing\n");
          goto error;
        }
      }
      else if(cmd == std::wstring(L"unsubscribe")) {
        // just clear subscribe list
        wprintf(L"Clearing subscriptions.\n");
        subscribe_map.clear();
      }
      else if(cmd == std::wstring(L"write")) {
        if(!write(main_object)) {
          wprintf(L"Error writing\n");
          goto error;
        }
      } else goto error;
         
    } else goto error;

    delete main_object;
  }  else goto error;

  return;

error:
  amq_send(std::wstring(L"invalid request: ") + text);
  return;
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
  setlocale(LC_ALL,"");
  if(argc!=2 && argc!=4) {
    print_usage();
    return 1;
  }
  char *address=argv[1];
  int rack=0,slot=0;
  if(argc==4) {
    rack=atoi(argv[2]);
    slot=atoi(argv[3]);
  }

  plc= new TS7Client();
  if(plc_connect(address,rack,slot)) {
    print_plc_info();

    // init active mq connection
    amq_init(L"tcp://172.16.205.61:61616", &msg_func, &poll_func);

    // main loop in here
    amq_mainloop();

    // should never get here
    amq_shutdown();
    plc_disconnect();
  }
  delete plc;
  return 0;
}
