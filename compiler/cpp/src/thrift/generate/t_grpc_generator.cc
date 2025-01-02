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
    generate_require();

    for (t_enum* enm : program_->get_enums()) {
      generate_enum(enm);
    }
    for (t_typedef* ttypedef : program_->get_typedefs()) {
      generate_typedef(ttypedef);
    }
    for (t_struct* strct : program_->get_structs()) {
      generate_struct(strct);
    }
    for (t_service* svc : program_->get_services()) {
      generate_service(svc);
    }

    close_generator();
  }

  static bool is_valid_namespace(const std::string& sub_namespace) {
    (void)sub_namespace;
    return true;
  }

  std::string display_name() const override { return "gRPC Generator"; }

  void generate_typedef(t_typedef* ttypedef) override {
    f_proto_ << "message " << to_pascal_case(ttypedef->get_symbolic()) << " {\n";
    f_proto_ << "  " << convert_type(ttypedef->get_type()) << " value = 1;\n";
    f_proto_ << "}\n\n";
  }

private:
  std::ofstream f_proto_;

  void generate_syntax() { f_proto_ << "syntax = \"proto3\";\n\n"; }

  void generate_package() {
    std::string grpc_namespace = namespace_path(program_);
    if (program_->get_namespace("grpc").empty()) {
      grpc_namespace = "default_package";
    }
    f_proto_ << "package " << grpc_namespace << ";\n\n";
  }

  void generate_require() {
    for (const t_program* program : program_->get_includes()) {
      f_proto_ << "import " << namespace_path(program) << ".proto;\n";
    }
    f_proto_ << "\n";
  }

  void generate_struct(t_struct* strct) override {
    f_proto_ << "message " << to_pascal_case(strct->get_name()) << " {\n";
    int field_id = 1;
    for (const t_field* field : strct->get_members()) {
      f_proto_ << "  "
               << prefix(field) + convert_type(field->get_type()) + " "
                      + to_lower_snake_case(field->get_name()) + " = " + std::to_string(field_id++)
               << ";\n";
    }
    f_proto_ << "}\n\n";
  }

  void generate_enum(t_enum* enm) override {
    const std::vector<t_enum_value*> constants = enm->get_constants();

    bool has_zero_value = false;
    for (const t_enum_value* value : constants) {
      if (value->get_value() == 0) {
        has_zero_value = true;
        break;
      }
    }

    f_proto_ << "enum " << to_pascal_case(enm->get_name()) << " {\n";
    if (!has_zero_value) {
      f_proto_ << "  " << to_upper_snake_case(enm->get_name()) << "_UNSPECIFIED = 0;\n";
    }

    for (const t_enum_value* value : enm->get_constants()) {
      std::string enum_name = to_upper_snake_case(value->get_name());
      f_proto_ << "  " << enum_name << " = " << value->get_value() << ";\n";
    }
    f_proto_ << "}\n\n";
  }

  void generate_service(t_service* svc) override {
    for (const t_function* func : svc->get_functions()) {
      t_struct* request_args_struct = func->get_arglist();
      std::string request_message_name = to_pascal_case(func->get_name()) + "PRequest";
      f_proto_ << "message " << request_message_name << " {\n";
      int field_id = 1;
      for (const t_field* arg : request_args_struct->get_members()) {
        f_proto_ << "  " << convert_type(arg->get_type()) << " "
                 << to_lower_snake_case(arg->get_name()) << " = " << field_id++ << ";\n";
      }
      f_proto_ << "}\n\n";

      if (!(func->get_returntype()->is_typedef())) {
        std::string response_message_name = to_pascal_case(func->get_name()) + "PResponse";
        f_proto_ << "message " << response_message_name << " {\n";
        if (!(func->get_returntype()->is_void())) {
          f_proto_ << "  " << convert_type(func->get_returntype()) << " value = 1;\n";
        }
        f_proto_ << "}\n\n";
      }
    }

    f_proto_ << "service " << to_pascal_case(svc->get_name()) << " {\n";

    for (const t_function* func : svc->get_functions()) {
      std::string request_message_name = to_pascal_case(func->get_name()) + "PRequest";
      std::string response_message_name;

      if (func->get_returntype()->is_typedef()) {
        response_message_name = to_pascal_case(func->get_returntype()->get_name());
      } else {
        response_message_name = to_pascal_case(func->get_name()) + "PResponse";
      }

      f_proto_ << "  rpc " << to_pascal_case(func->get_name()) << " (" << request_message_name
               << ") returns (" << response_message_name << ");\n";
    }

    const t_service* parent_service = svc->get_extends();
    if (parent_service != nullptr) {

      for (const t_function* func : parent_service->get_functions()) {
        std::string request_message_name = to_pascal_case(func->get_name()) + "PRequest";
        std::string response_message_name;

        if (func->get_returntype()->is_typedef()) {
          response_message_name = to_pascal_case(func->get_returntype()->get_name());
        } else {
          response_message_name = to_pascal_case(func->get_name()) + "PResponse";
        }

        f_proto_ << "  rpc " << to_pascal_case(func->get_name()) << " (" <<  namespace_path(parent_service->get_program()) << "."
                 << request_message_name << ") returns (" <<  namespace_path(parent_service->get_program()) << "."
                 << response_message_name << ");\n";
      }
    }

    f_proto_ << "}\n\n";
  }

  std::string namespace_path(const t_program* program) {
    return to_lower_snake_case(program->get_namespace("grpc"));
  }

  std::string to_pascal_case(const std::string& name) {
    std::ostringstream result;
    bool capitalize_next = true;

    for (char c : name) {
      if (std::isalnum(c)) {
        if (capitalize_next) {
          result << static_cast<char>(std::toupper(c));
          capitalize_next = false;
        } else {
          result << c;
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
        result << "_";
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
      const t_base_type* base_type = static_cast<const t_base_type*>(type);
      switch (base_type->get_base()) {
      case t_base_type::TYPE_VOID:
        throw std::runtime_error("Unsupported void type in message struct");
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
      const t_list* list_type = static_cast<const t_list*>(type);
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
    } else if (type->is_typedef() || type->is_enum() || type->is_struct()) {
      // Check if the type belongs to another program (package)
      if (const t_program* type_program = type->get_program()) {
        if (type_program != program_) {
          if (!(type_program->get_namespace("grpc").empty())) {
            return namespace_path(type_program) + "." + to_pascal_case(type->get_name());
          }
        }
      }

      return to_pascal_case(type->get_name());
    } else {
      throw std::runtime_error("Unsupported type " + type->get_name());
    }
  }
};

THRIFT_REGISTER_GENERATOR(grpc, "gRPC", "Generate gRPC-compatible .proto files");
