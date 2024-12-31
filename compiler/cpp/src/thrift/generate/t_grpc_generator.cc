#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <string>

#include "thrift/generate/t_generator.h"
#include "thrift/platform.h"

using std::map;
using std::ofstream;
using std::ostringstream;
using std::string;

class t_grpc_generator : public t_generator {
public:
  t_grpc_generator(t_program* program,
                   const std::map<std::string, std::string> parsed_options,
                   const std::string& option_string)
    : t_generator(program) {
    // (void)option_string;
    // std::map<std::string, std::string>::const_iterator iter;

    // TODO: parsed_options,option_string でmergeや、service|message|enum でのfile分割、name space
    // 単位でのfile分割、 proto2などのバージョンサポートを行う
    (void)parsed_options;
    (void)option_string;

    out_dir_base_ = "gen-grpc";
  }

  void init_generator() override {
    MKDIR(get_out_dir().c_str());

    std::string filename = get_out_dir() + to_lower_snake_case(program_->get_name()) + ".proto";
    f_proto_.open(filename.c_str());

    if (!f_proto_.is_open()) {
      throw std::runtime_error("Could not open output file for .proto generation");
    }
  }

  void close_generator() override { f_proto_.close(); }

  void generate_program() override {
    init_generator();
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

  static bool is_valid_namespace(const std::string& sub_namespace) {
    (void)sub_namespace;
    return true;
  }

  std::string display_name() const override {
    return "gRPC Generator";
  }

  void generate_typedef(t_typedef* ttypedef) override {
    (void)ttypedef;
  }

private:
  std::ofstream f_proto_;

  void generate_syntax() { f_proto_ << "syntax = \"proto3\";\n\n"; }

  void generate_package() {
    std::string grpc_namespace = program_->get_namespace("grpc");
    if (grpc_namespace.empty()) {
        grpc_namespace = "default_package";
    }
    f_proto_ << "package " << grpc_namespace << ";\n\n";
  }

  void generate_struct(t_struct* strct) override {
    f_proto_ << "message " << to_pascal_case(strct->get_name()) << " {\n";
    int field_id = 1;
    for (const auto& field : strct->get_members()) {
      f_proto_ << "  "
               << prefix(field) + convert_type(field->get_type()) + " "
                      + to_lower_snake_case(field->get_name()) + " = " + std::to_string(field_id++)
               << ";\n";
    }
    f_proto_ << "}\n\n";
  }

  void generate_enum(t_enum* enm) override {
    f_proto_ << "enum " << to_pascal_case(enm->get_name()) << " {\n";
    for (const auto& value : enm->get_constants()) {
      // If the value is 0, add the suffix "UNSPECIFIED"
      std::string enum_name = to_upper_snake_case(value->get_name());
      if (value->get_value() == 0) {
        enum_name += "_UNSPECIFIED";
      }
      f_proto_ << "  " << enum_name << " = " << value->get_value() << ";\n";
    }
    f_proto_ << "}\n\n";
  }

  void generate_service(t_service* svc) override{
    f_proto_ << "service " << to_pascal_case(svc->get_name()) << " {\n";
    for (const auto& func : svc->get_functions()) {
        std::string request_type = to_pascal_case(func->get_name());
        std::string response_type = convert_type(func->get_returntype());

        // generate_function_args(func, request_type);
        // generate_function_response(func, response_type);

        f_proto_ << "  rpc " << to_pascal_case(func->get_name()) << " ("
                 << request_type << ") returns (" << response_type << ");\n";
    }
    f_proto_ << "}\n\n";
  }

  // void generate_function_args(const t_function* func, const std::string& message_name) {
  //   f_proto_ << "message " << message_name << " {\n";
  //   int field_id = 1;

  //   for (const auto& arg : func->get_arglist()->get_members()) {
  //       f_proto_ << "  " << convert_type(arg->get_type()) << " "
  //                << to_lower_snake_case(arg->get_name()) << " = " << field_id++ << ";\n";
  //   }

  //   f_proto_ << "}\n\n";
  // }

  // void generate_function_response(const t_function* func, const std::string& message_name) {
  //   f_proto_ << "message " << message_name << " {\n";

  //   if (!func->get_returntype()->is_void()) {
  //       f_proto_ << "  " << convert_type(func->get_returntype()) << " value = 1;\n";
  //   }

  //   f_proto_ << "}\n\n";
  // }

  std::string to_pascal_case(const std::string& name) {
    std::ostringstream result;
    bool capitalize_next = true;

    for (char c : name) {
      if (std::isalnum(c)) {
        if (capitalize_next) {
          result << static_cast<char>(std::toupper(c));
          capitalize_next = false;
        } else {
          result << static_cast<char>(std::tolower(c));
        }
      } else {
        capitalize_next = true;
      }
    }
    return result.str();
  }

  std::string to_lower_snake_case(const std::string& name) {
    std::ostringstream result;

    for (size_t i = 0; i < name.size(); ++i) {
      char c = name[i];
      if (std::isupper(c)) {
        if (i > 0) {
          result << "_";
        }
        result << static_cast<char>(std::tolower(c));
      } else {
        result << c;
      }
    }
    return result.str();
  }

  std::string to_upper_snake_case(const std::string& name) {
    std::ostringstream result;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (std::isupper(c) && i > 0 && std::islower(name[i - 1])) {
            result << "_";  // 大文字の直前にアンダースコアを追加
        }
        result << static_cast<char>(std::toupper(c));
    }
    return result.str();
}

  std::string prefix(const t_field* field) {
    switch (field->get_req()) {
    case t_field::T_OPTIONAL:
      return "optional ";
    case t_field::T_REQUIRED:
      return "";
    default:
      return "";
      break;
    }
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
    } else if (type->is_map() || type->is_set()) {
      t_type* k_type = ((t_map*)type)->get_key_type();
      t_type* v_type = ((t_map*)type)->get_val_type();

      std::string k_type_name = k_type->get_name();
      if (k_type_name != "string" && k_type_name != "int32" && k_type_name != "int64") {
        throw std::runtime_error("Unsupported key type for Proto3 map: " + k_type_name);
      }

      return "map<" + convert_type(k_type) + "," + convert_type(v_type) + ">";
      //   throw std::runtime_error("Maps and sets are not supported directly in proto3");
    } else if (type->is_uuid()) {
      // Encording grpc string type. Reason for grpc is not support uuid type.
      return "string";
    } else if (type->is_binary()) {
      return "bytes";
    } else {
      return to_pascal_case(type->get_name());
    }
  }
};

THRIFT_REGISTER_GENERATOR(grpc, "gRPC", "Generate gRPC-compatible .proto files");
