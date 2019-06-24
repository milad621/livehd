//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <atomic>
#include <fstream>

#include "eprp_utils.hpp"
#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"

#include "inou_graphviz.hpp"

void setup_inou_graphviz() {
  Inou_graphviz p;
  p.setup();
}

void Inou_graphviz::setup() {
  Eprp_method m1("inou.graphviz.fromlg", "export lgraph to graphviz dot format", &Inou_graphviz::fromlg);

  m1.add_label_optional("bits",    "dump bits (true/false)", "false");
  m1.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");
  //m1.add_label_optional("odir",    "path to put the dot", ".");

  register_inou(m1);

  Eprp_method m2("inou.graphviz.fromlnast", "export lnast to graphviz dot format", &Inou_graphviz::fromlnast);

  m2.add_label_required("files",  "cfg_text files to process (comma separated)");
  m2.add_label_optional("odir",   "path to put the dot", ".");

  register_inou(m2);
}

Inou_graphviz::Inou_graphviz() : Pass("graphviz") {
  bits    = false;
  verbose = false;
  files   = "";
  odir    = ".";
}


void Inou_graphviz::fromlg(Eprp_var &var) {
  Inou_graphviz p;

  p.odir    = var.get("odir");
  p.bits    = var.get("bits") == "true";
  p.verbose = var.get("verbose") == "true";

  bool ok = p.setup_directory(p.odir);
  if (!ok) return;

  std::vector<LGraph *> lgs;
  for (const auto &l : var.lgs) {
    lgs.push_back(l);
  }

  p.do_fromlg(lgs);
}

void Inou_graphviz::do_fromlg(std::vector<LGraph *> &lgs) {
  for (const auto &lg_parent : lgs) {
    populate_lg_data(lg_parent);
    lg_parent->each_sub_fast([lg_parent, this](Node &node, Lg_type_id lgid) {
      fmt::print("subgraph lgid:{}\n", lgid);
      LGraph *lg_child = LGraph::open(lg_parent->get_path(), lgid);
      populate_lg_data(lg_child);
    });
  }
}

void Inou_graphviz::populate_lg_data(LGraph *g) {
  std::string data = "digraph {\n";

  g->each_node_fast([&data](const Node &node) {
    auto node_name = node.has_name() ? node.get_name() : "";

    if (node.get_type().op == U32Const_Op)
      data += fmt::format(" {} [label=\"{} :{} :{} :{}\"];\n"
             , node.debug_name(), node.debug_name(), node.get_type().get_name()
             , node_name, node.get_type_const_value());
    else
      data += fmt::format(" {} [label=\"{} :{} :{}\"];\n"
             , node.debug_name(), node.debug_name(), node.get_type().get_name(), node_name);


    for (auto &out : node.out_edges()) {
      auto &dpin       = out.driver;
      auto  dnode_name = dpin.get_node().debug_name();
      auto  snode_name = out.sink.get_node().debug_name();
      auto  dpin_name  = dpin.has_name() ? dpin.get_name() : "";
      auto  bits       = dpin.get_bits();

      data += fmt::format(" {}->{}[label=\"{}b :{} :{} :{}\"];\n"
          , dnode_name, snode_name, bits, dpin.get_pid(), out.sink.get_pid(), dpin_name);
    }
  });

  g->each_graph_output([&data](const Node_pin &pin) {
    std::string_view dst_str = "virtual_dst_module";
    auto             bits    = pin.get_bits();
    data += fmt::format(" {}->{}[label=\"{}b\"];\n", pin.get_node().debug_name(), dst_str, bits);
  });

  data += "}\n";

  std::string file = absl::StrCat(odir, "/", g->get_name(), ".dot");
  int         fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    Pass::error("inou.graphviz unable to create {}", file);
    return;
  }
  write(fd, data.c_str(), data.size());
  close(fd);
}



void Inou_graphviz::fromlnast(Eprp_var &var) {
  Inou_graphviz p;
  p.files   = var.get("files");
  p.odir    = var.get("odir");

  bool ok = p.setup_directory(p.odir);
  if (!ok) return;

  //cfg_text to lnast
  p.memblock = p.setup_memblock(p.files);
  p.lnast_parser.parse("lnast", p.memblock);

  //lnast->ssa_trans();
  p.do_fromlnast(p.files);
}

void Inou_graphviz::do_fromlnast( std::string_view files) {
  populate_lnast_data(files);
}

void Inou_graphviz::populate_lnast_data(std::string_view files) {
  const auto& lnast = lnast_parser.get_ast().get(); //unique_ptr lend its ownership
  std::string data = "digraph {\n";

  for(const auto& itr : lnast->depth_preorder(lnast->get_root())){
    const auto &node_data = lnast->get_data(itr);
    const auto &type  = lnast_parser.ntype_dbg(node_data.type);
    const auto &scope = node_data.scope;
    std::string name(node_data.token.get_text(memblock));
    if(node_data.type == Lnast_ntype_top)
      name = "top";


    auto id = std::to_string(itr.level)+std::to_string(itr.pos);
    data += fmt::format(" {} [label=\"{}:{}:{}\"];\n", id, type, name, scope);
    if(node_data.type == Lnast_ntype_top)
      continue;

    //get parent data for link
    const auto &p = lnast->get_parent(itr);
    const auto &ptype = lnast->get_data(p).type;
    std::string pname(lnast->get_data(p).token.get_text(memblock));
    if(ptype == Lnast_ntype_top)
      pname = "top";

    auto parent_id = std::to_string(p.level)+std::to_string(p.pos);
    data += fmt::format(" {}->{};\n", parent_id, id);
  }

  data += "}\n";

  std::string file = absl::StrCat(odir, "/", files, ".dot");
  int         fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    Pass::error("inou.graphviz_lnast unable to create {}", file);
    return;
  }
  write(fd, data.c_str(), data.size());
  close(fd);
}

std::string_view Inou_graphviz::setup_memblock(std::string_view files){
  std::string file_path(files);
  int fd = open(file_path.c_str(), O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "error, could not open %s\n", file_path.c_str());
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);
  printf("Size: %lu\n", (uint64_t)sb.st_size);

  char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  fprintf(stderr, "Content of memblock: \n%s\n", memblock);
  if(memblock == MAP_FAILED) {
    fprintf(stderr, "error, mmap failed\n");
    exit(-3);
  }
  return memblock;
}
