#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <list>
#include <filesystem>

#include "thrift/platform.h"
#include "thrift/generate/t_generator.h"

using std::map;
using std::ofstream;
using std::ostringstream;
using std::pair;
using std::string;
using std::stringstream;
using std::vector;

class t_grpc_generator : public t_generator {
 public:
  t_grpc_generator(t_program* program, 
                   const std::map<std::string, std::string> parsed_options, 
                   const std::string& option_string)
      : t_generator(program) {
    // (void)option_string;
    // std::map<std::string, std::string>::const_iterator iter;

    //TODO: parsed_options,option_string でmergeや、service|message|enum でのfile分割、name space 単位でのfile分割、
    //proto2などのバージョンサポートを行う
    

    out_dir_base_ = "gen-grpc";
  }

  void init_generator() override {
    MKDIR(get_out_dir().c_str());

    std::string filename = get_out_dir() + program_->get_name() + ".proto";
    f_proto_.open(filename.c_str());

    if (!f_proto_.is_open()) {
      throw std::runtime_error("Could not open output file for .proto generation");
    }
  }

  void close_generator() override {
    f_proto_.close();
  }

  void generate_program() override {
    generate_syntax();
    generate_package();

    for (auto* strct : program_->get_structs()) {
      generate_struct(strct);
    }
    for (auto* enm : program_->get_enums()) {
      generate_enum(enm);
    }
    for (auto* svc : program_->get_services()) {
      generate_service(svc);
    }
  }

 private:
  std::ofstream f_proto_;

  void generate_syntax() {
    f_proto_ << "syntax = \"proto3\";\n\n";
  }

  void generate_package() {
    f_proto_ << "package " << program_->get_namespace("grpc") << ";\n\n";
  }

  void generate_struct(const t_struct* strct) {
    f_proto_ << "message " << strct->get_name() << " {\n";
    int field_id = 1;
    for (const auto& field : strct->get_members()) {
      f_proto_ << "  " << convert_type(field->get_type()) << " "
               << field->get_name() << " = " << field_id++ << ";\n";
    }
    f_proto_ << "}\n\n";
  }

  void generate_enum(const t_enum* enm) {
    f_proto_ << "enum " << enm->get_name() << " {\n";
    for (const auto& value : enm->get_constants()) {
      f_proto_ << "  " << value->get_name() << " = " << value->get_value() << ";\n";
    }
    f_proto_ << "}\n\n";
  }

  void generate_service(const t_service* svc) {
    f_proto_ << "service " << svc->get_name() << " {\n";
    for (const auto& func : svc->get_functions()) {
      f_proto_ << "  rpc " << func->get_name() << " ("
               << func->get_arglist()->get_name() << ") returns ("
               << func->get_returntype()->get_name() << ");\n";
    }
    f_proto_ << "}\n\n";
  }

  std::string convert_type(const t_type* type) {
    if (type->is_base_type()) {
      const auto* base_type = static_cast<const t_base_type*>(type);
      switch (base_type->get_base()) {
        case t_base_type::TYPE_VOID:
          return "google.protobuf.Empty";
        case t_base_type::TYPE_STRING:
          return "string";
        case t_base_type::TYPE_BOOL:
          return "bool";
        case t_base_type::TYPE_I8:
        case t_base_type::TYPE_I16:
        case t_base_type::TYPE_I32:
          return "int32";
        case t_base_type::TYPE_I64:
          return "int64";
        case t_base_type::TYPE_DOUBLE:
          return "double";
        default:
          throw std::runtime_error("Unsupported base type");
      }
    } else if (type->is_list()) {
      const auto* list_type = static_cast<const t_list*>(type);
      return "repeated " + convert_type(list_type->get_elem_type());
    } else if (type->is_map()|| type->is_set()) {
      t_type* k_type = ((t_map*)type)->get_key_type();
      t_type* v_type = ((t_map*)type)->get_val_type();    
      
      return  "map<" + convert_type(k_type) + "," + convert_type(v_type) + ">";
      //   throw std::runtime_error("Maps and sets are not supported directly in proto3"); 
    } else if (type -> is_uuid()) {
      // Encording grpc string type. Reason for grpc is not support uuid type.
        return "string";
    } else if (type -> is_binary()) {
        return "bytes";
    } else {
      return type->get_name();
    }
  }
};

THRIFT_REGISTER_GENERATOR(grpc, "gRPC", "Generate gRPC-compatible .proto files");
