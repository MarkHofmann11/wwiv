/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.x                          */
/*                    Copyright (C)2020, WWIV Software Services           */
/*                                                                        */
/*    Licensed  under the  Apache License, Version  2.0 (the "License");  */
/*    you may not use this  file  except in compliance with the License.  */
/*    You may obtain a copy of the License at                             */
/*                                                                        */
/*                http://www.apache.org/licenses/LICENSE-2.0              */
/*                                                                        */
/*    Unless  required  by  applicable  law  or agreed to  in  writing,   */
/*    software  distributed  under  the  License  is  distributed on an   */
/*    "AS IS"  BASIS, WITHOUT  WARRANTIES  OR  CONDITIONS OF ANY  KIND,   */
/*    either  express  or implied.  See  the  License for  the specific   */
/*    language governing permissions and limitations under the License.   */
/*                                                                        */
/**************************************************************************/
#include "core/parser/ast.h"

#include "core/stl.h"
#include "core/strings.h"
#include "fmt/format.h"
#include <array>
#include <iomanip>
#include <ios>
#include <memory>
#include <random>
#include <sstream>
#include <stack>
#include <stdexcept>

using namespace wwiv::strings;

namespace wwiv::core::parser {


parse_error::parse_error(const std::string& m)
      : ::std::runtime_error(m) {}

      ///////////////////////////////////////////////////////////////////////////
// Factor

std::string to_string(Operator o) {
  switch (o) {
  case Operator::add:
    return "add";
  case Operator::div:
    return "div";
  case Operator::eq:
    return "eq";
  case Operator::ge:
    return "ge";
  case Operator::gt:
    return "gt";
  case Operator::le:
    return "le";
  case Operator::mul:
    return "mul";
  case Operator::ne:
    return "ne";
  case Operator::sub:
    return "sub";
  case Operator::logical_or:
    return "or";
  case Operator::logical_and:
    return "and";
  default:
    return fmt::format("UNKNOWN ({})", static_cast<int>(o));
  }
}


std::string to_symbol(Operator o) {
  switch (o) {
  case Operator::add:
    return "+";
  case Operator::div:
    return "/";
  case Operator::eq:
    return "==";
  case Operator::ge:
    return ">=";
  case Operator::gt:
    return ">";
  case Operator::le:
    return "<=";
  case Operator::lt:
    return "<";
  case Operator::mul:
    return "*";
  case Operator::ne:
    return "!=";
  case Operator::sub:
    return "-";
  case Operator::logical_or:
    return "||";
  case Operator::logical_and:
    return "&&";
  default:
    return fmt::format("UNKNOWN ({})", static_cast<int>(o));
  }
}

std::string to_string(AstType t) {
  switch (t) {
  case AstType::LOGICAL_OP:
    return "LOGICAL_OP";
  case AstType::BINOP:
    return "binop";
  case AstType::UNOP:
    return "unop";
  case AstType::PAREN:
    return "paren";
  case AstType::EXPR:
    return "expr";
  case AstType::FACTOR:
    return "factor";
  case AstType::TAUTOLOGY:
    return "tautology";
  case AstType::ERROR:
    return "error";
  case AstType::ROOT:
    return "root";
  }
  return "unknown";
}

///////////////////////////////////////////////////////////////////////////
// Factor

Factor::Factor(FactorType t, int v, const std::string& s)
    : Expression(AstType::FACTOR), factor_type_(t), val(v), sval(s) {}

std::string to_string(FactorType t) {
  switch (t) {
  case FactorType::int_value:
    return "int_value";
  case FactorType::string_val:
    return "string_val";
  case FactorType::variable:
    return "variable";
  }
  return "FactorType::UNKNOWN";
}

std::string Factor::ToString(int indent) {
  auto pad = std::string(indent, ' ');
  return fmt::format("{}Factor #{}: {} '{}'", pad, id(), to_string(factor_type_),
                     factor_type_ == FactorType::int_value ? std::to_string(val) : sval);
}

void Factor::accept(AstVisitor* visitor) { visitor->visit(this); }

static std::unique_ptr<Factor> createFactor(const Token& token) {
  const auto& l = token.lexeme;

  if (token.type == TokenType::string || token.type == TokenType::character) {
    return std::make_unique<String>(l);
  } else if (isdigit(l.front())) {
    return std::make_unique<Number>(to_number<int>(l));
  } else {
    return std::make_unique<Variable>(l);
  }
}

static std::unique_ptr<LogicalOperatorNode> createLogicalOperator(const Token& token) {
  switch (token.type) {
  case TokenType::logical_or:
    return std::make_unique<LogicalOperatorNode>(Operator::logical_or);
  case TokenType::logical_and:
    return std::make_unique<LogicalOperatorNode>(Operator::logical_and);
  }
  LOG(ERROR) << "Should never happen: " << static_cast<int>(token.type) << ": " << token.lexeme;
  return {};
}

static std::unique_ptr<BinaryOperatorNode> createBinaryOperator(const Token& token) {
  switch (token.type) {
  case TokenType::add:
    return std::make_unique<BinaryOperatorNode>(Operator::add);
  case TokenType::assign:
    LOG(FATAL) << "return std::make_unique<BinaryOperatorNode>(Operator::ass);";
  case TokenType::div:
    return std::make_unique<BinaryOperatorNode>(Operator::div);
  case TokenType::eq:
    return std::make_unique<BinaryOperatorNode>(Operator::eq);
  case TokenType::ge:
    return std::make_unique<BinaryOperatorNode>(Operator::ge);
  case TokenType::gt:
    return std::make_unique<BinaryOperatorNode>(Operator::gt);
  case TokenType::le:
    return std::make_unique<BinaryOperatorNode>(Operator::le);
  case TokenType::lt:
    return std::make_unique<BinaryOperatorNode>(Operator::lt);
  case TokenType::mul:
    return std::make_unique<BinaryOperatorNode>(Operator::mul);
  case TokenType::ne:
    return std::make_unique<BinaryOperatorNode>(Operator::ne);
  case TokenType::sub:
    return std::make_unique<BinaryOperatorNode>(Operator::sub);
  }
  LOG(ERROR) << "Should never happen: " << static_cast<int>(token.type) << ": " << token.lexeme;
  return {};
}

bool Ast::reduce(std::stack<std::unique_ptr<AstNode>>& stack) {
  auto expr = std::make_unique<Expression>();
  auto right = std::move(stack.top());
  stack.pop();
  auto op = std::move(stack.top());
  stack.pop();
  auto left = std::move(stack.top());
  stack.pop();

  // Set op
  if (op->ast_type() == AstType::BINOP) {
    auto oper = dynamic_cast<BinaryOperatorNode*>(op.get());
    expr->op_ = oper->oper;
  } else if (op->ast_type() == AstType::LOGICAL_OP) {
    auto oper = dynamic_cast<LogicalOperatorNode*>(op.get());
    expr->op_ = oper->oper;
  } else {
    stack.push(
        std::make_unique<ErrorNode>(StrCat("Expected BINOP at: ", static_cast<int>(op->ast_type()))));
    return false;
  }
  // Set left
  if (left->ast_type() == AstType::FACTOR) {
    auto factor = dynamic_cast<Factor*>(left.release());
    expr->left_ = std::unique_ptr<Factor>(factor);
  } else if (left->ast_type() == AstType::EXPR) {
    auto factor = dynamic_cast<Expression*>(left.release());
    expr->left_ = std::unique_ptr<Expression>(factor);
  }
  // Set right
  if (right->ast_type() == AstType::FACTOR) {
    auto factor = dynamic_cast<Factor*>(right.release());
    expr->right_ = std::unique_ptr<Factor>(factor);
  } else if (right->ast_type() == AstType::EXPR) {
    auto factor = dynamic_cast<Expression*>(right.release());
    expr->right_ = std::unique_ptr<Expression>(factor);
  }
  stack.push(std::move(expr));
  return true;
}

std::unique_ptr<AstNode> Ast::parseExpression(std::vector<Token>::iterator& it,
                                              const std::vector<Token>::iterator& end) {
  std::stack<std::unique_ptr<AstNode>> stack;
  for (; it != end; it++) {
    switch (it->type) { 
    case TokenType::rparen: {
      // If we have a right parens, we are no longer in an expression.
      // exit now before advancing it;

      // Flatten out any repeating logical conditions here,  we should end with
      // one single tree node at the root.
      while (stack.size() > 1) {
        if (!reduce(stack)) {
          break;
        }
      }
      if (stack.size() != 1) {
        LOG(ERROR) << "The Stack size should be one at the root, we have: " << stack.size();
      }
      auto ret = std::move(stack.top());
      stack.pop();
      return ret;
    }
    case TokenType::comment:
      // skip
      LOG(INFO) << "comment: " << it->lexeme;
      break;
    case TokenType::string:
    case TokenType::character:
    case TokenType::number:
    case TokenType::identifier: {
      const auto l = it->lexeme;
      if (l.empty()) {
        return std::make_unique<ErrorNode>(
            StrCat("Unable to parse expression starting at: ", it->lexeme));
      }
      const auto nr = need_reduce(stack, false);
      stack.push(createFactor(*it));
      if (nr) {
        // One identifier and one operator
        reduce(stack);
      }
    } break;
    case TokenType::add:
    case TokenType::assign:
    case TokenType::div:
    case TokenType::eq:
    case TokenType::ge:
    case TokenType::gt:
    case TokenType::le:
    case TokenType::lt:
    case TokenType::mul:
    case TokenType::ne:
    case TokenType::sub: {
      stack.push(createBinaryOperator(*it));
    } break;
    case TokenType::logical_or: 
    case TokenType::logical_and: {
      stack.push(createLogicalOperator(*it));
    } break;
    default: {
      LOG(INFO) << "Unexpected token: " << *it;
    } break;
      // ignore
    }
  }
  // Flatten out any repeating logical conditions here,  we should end with
  // one single tree node at the root.
  while (stack.size() > 1) {
    if (!reduce(stack)) {
      break;
    }
  }
  if (stack.size() != 1) {
    LOG(ERROR) << "The Stack size should be one at the root, we have: " << stack.size();
  }
  auto ret = std::move(stack.top());
  stack.pop();
  return ret;
}

std::unique_ptr<AstNode> Ast::parseGroup(std::vector<Token>::iterator& it,
                                         const std::vector<Token>::iterator& end) {
  if ((it+1) == end) {
    return std::make_unique<ErrorNode>(
        StrCat("Unable to parse expression starting at: ", it->lexeme));
  }
  // Skip lparen
  ++it;
  std::stack<std::unique_ptr<AstNode>> stack;
  while (it != end && it->type != TokenType::rparen) {
    auto expr = parseExpression(it, end);
    if (!expr) {
      return std::make_unique<ErrorNode>(
          StrCat("Unable to parse expression starting at: ", it->lexeme));
    }
    stack.push(std::move(expr));
    if (it == end || it->type != TokenType::rparen) {
      LOG(ERROR) << "Missing right parens";
      auto pos = it == end ? "end" : it->lexeme;
      return std::make_unique<ErrorNode>(StrCat("Unable to parse expression starting at: ", pos));
    }
  }
  // Flatten out any repeating logical conditions here,  we should end with
  // one single tree node at the root.
  while (stack.size() > 1) {
    if (!reduce(stack)) {
      break;
    }
  }
  if (stack.size() != 1) {
    LOG(ERROR) << "The Stack size should be one at the root, we have: " << stack.size();
  }
  auto root = std::move(stack.top());
  stack.pop();
  return root;
}

/** 
 * True if we need to reduce the stack.  We only allow logical_operations to trigger
 * reduce if we are betwen expressions (this happens at partse, not at parseExpression
 */
bool Ast::need_reduce(const std::stack<std::unique_ptr<AstNode>>& stack, bool allow_logical) {
  if (stack.empty()) {
    return false;
  }
  const auto t = stack.top()->ast_type();
  bool nr = t == AstType::BINOP;
  if (allow_logical && t == AstType::LOGICAL_OP) {
    return true;
  }
  return nr;
}  

std::unique_ptr<RootNode> Ast::parse(
    std::vector<Token>::iterator& begin, const std::vector<Token>::iterator& end) {
  std::stack<std::unique_ptr<AstNode>> stack;
  for (auto it = begin; it != end;) {
    const auto& t = *it;
    switch (t.type) { 
    case TokenType::lparen: {
      auto expr = parseGroup(it, end);
      if (!expr || it == end) {
        return std::make_unique<RootNode>(std::make_unique<ErrorNode>(
            StrCat("Unable to parse expression starting at: ", it->lexeme)));
      }
      stack.push(std::move(expr));
      } break;

    case TokenType::logical_and:
    case TokenType::logical_or: {
      stack.push(createLogicalOperator(*it));
    } break;

    default: {
      // Try to reduce.
      bool nr = need_reduce(stack, true);
      auto expr = parseExpression(it, end);
      if (!expr) {
        return std::make_unique<RootNode>(std::make_unique<ErrorNode>(
            StrCat("Unable to parse expression starting at: ", it->lexeme)));
      }
      stack.push(std::move(expr));
      if (nr) {
        reduce(stack);
      }
    } break;
    }

    if (it != end) {
      ++it;
    }
  }
  if (stack.size() != 1) {
    LOG(ERROR) << "The Stack size should be one at the root, we have: " << stack.size();
  }
  auto root = std::make_unique<RootNode>(std::move(stack.top()));
  stack.pop();
  return root;
}

bool Ast::parse(const Lexer& l) {
  auto tokens = l.tokens();
  auto it = std::begin(tokens);
  root_ = parse(it, std::end(tokens));
  if (!root_) {
    return false;
  }
  return root_->node->ast_type() != AstType::ERROR;
}

AstNode* Ast::root() { 
  return root_->node.get();
}

std::string AstNode::ToString() { 
  return fmt::format("AstNode: {}", to_string(ast_type_)); }

void AstNode::accept(AstVisitor* visitor) { 
  auto* expr = dynamic_cast<Expression*>(this);
  if (expr) {
    visitor->visit(expr);
    return;
  }
  visitor->visit(this); 
}

int Expression::expression_id = 0;

std::string Expression::ToString() { return ToString(true, 0); }

std::string Expression::ToString(bool include_children) { return ToString(include_children, 0); }

std::string Expression::ToString(bool include_children, int indent) { 
  std::ostringstream ss;
  std::string pad{"  "};
  if (indent > 0) {
    //pad.append(fmt::format("({})", indent));
    pad.append(std::string(indent, ' '));
  }
  ss << "\r\n";
  ss << pad << "Expression (" << id_ << "): ";
  if (auto* l = dynamic_cast<Factor*>(left())) {
    ss << pad << "[Left: " << l->ToString(0) << "]";
  } else if (left_ && include_children) {
    ss << pad << "[Left: " << left_->ToString(indent + 4) << " ]";
  } else if (left_) {
    ss << pad << "[Left: EXPRESSION #" << left_->id() << "]";
  }
  ss << "\r\n";
  ss << pad << "[OP: '" << to_string(op_) << "'](" << id_ << ")";
  ss << "\r\n";
  if (auto* f = dynamic_cast<Factor*>(right())) {
    ss << pad << "[Right: " << f->ToString(0) << "]";
  } else if (right_ && include_children) {
    ss << pad << "[Right: " << right_->ToString(indent + 4) << "]";
  } else if (right_) {
    ss << pad << "[Right: EXPRESSION #" << right_->id() << "]";
  }
  return ss.str();
}

void Expression::accept(AstVisitor* visitor) { 
  if (auto* f = dynamic_cast<Factor*>(left())) {
    f->accept(visitor);
  } else if (auto* e = dynamic_cast<Expression*>(left())) {
    e->accept(visitor);
  }
  if (auto* r = dynamic_cast<Factor*>(right())) {
    r->accept(visitor);
  } else if (auto* e = dynamic_cast<Expression*>(right())) {
    e->accept(visitor);
  }
  visitor->visit(this);
}

std::string RootNode::ToString() { return fmt::format("ROOT: {}\n", node->ToString()); }


std::string BinaryOperatorNode::ToString() { 
  return fmt::format("BinOp: ", to_string(oper));
}

std::string LogicalOperatorNode::ToString() {
  return fmt::format("LogOp: ", to_string(oper));
}

} // namespace wwiv::core::parser
