#include <map>
#include <cstdint>
#include "opt.h"
#include "opt_util.h"


using namespace std;

// TODO:
// it seems we can use push/pop to reduce the size of formulas
// E.g., instead of adding x > 3, x > 10 step-by-step, we can delete x > 3 while adding x > 10
bool opt_solver::solve_with_linear_search(expr &pre_cond, expr &query,
		expr_vector &res, opt_mode mode) {
	if (m_verbose_lvl) {
		std::cout << "SymAbs: solving with linear search\n";
	}
	unsigned timeout = m_timeout;
	context &ctx = pre_cond.ctx();
	if (m_var_lge_zero) {
		pre_cond = pre_cond && uge(query, 0);
	}
	params p(ctx);
	p.set("timeout", timeout);

	if (mode == g_min || mode == g_min_max) {
		solver sol_min(ctx);
		sol_min.add(pre_cond);
		sol_min.set(p);
		// find lower bound
		try {
			expr lower = ctx.bv_val(0, query.get_sort().bv_size());
			while (sol_min.check() == z3::sat) {
				sol_min.pop();
				model m = sol_min.get_model();
				lower = m.eval(query);
				sol_min.push();
				//sol_min.add(query < lower);
				sol_min.add(ult(query, lower));
			}
			res.push_back(lower);
			if (m_verbose_lvl) {
				std::cout << "  min: " << lower << "\n";
			}
		} catch (z3::exception &ex) {
			res.push_back(ctx.bool_val(false));
		}
	}

	if (mode == g_max || mode == g_min_max) {
		solver sol_max(ctx);
		sol_max.add(pre_cond);
		expr cur_upper = ctx.bv_val(0, query.get_sort().bv_size());

		// 1. solve once (this step is not necessary)
		if (m_prune_unsat) {
			if (sol_max.check() == z3::sat) {
				m_linear_search_solver_call++;
				model m = sol_max.get_model();
				cur_upper = m.eval(query);
				sol_max.push();
				//expr cond_test = query > cur_upper;
				expr cond_test = ugt(query, cur_upper);
				sol_max.add(cond_test);
				TEST_SAT_UNDER_COND: check_result cond_ret = sol_max.check();
				m_linear_search_solver_call++;
				if (cond_ret == z3::sat) {
					m = sol_max.get_model();
					cur_upper = m.eval(query);
					sol_max.pop();
					//expr cond_test = query > cur_upper;
					expr cond_test = ugt(query, cur_upper);
					sol_max.push();
					sol_max.add(cond_test);
					goto TEST_SAT_UNDER_COND;
				} else if (cond_ret == z3::unsat) {
					res.push_back(cur_upper);
					m_linear_search_max = cur_upper.get_numeral_int();
					if (m_verbose_lvl) {
						std::cout << "  max: " << m_linear_search_max << "\n";
						std::cout << "  solver calls: "
								<< m_linear_search_solver_call << "\n";
					}
					return true;
				} else {
					res.push_back(cur_upper);
					return false;  // not exact bound
				}
			} else {
				return false;
			}
		}
	}
	return true;
}

// TODO...
bool opt_solver::solve_with_binary_search(expr &pre_cond, expr &query,
		expr_vector &res, opt_mode mode) {
	if (m_verbose_lvl) {
		std::cout << "SymAbs: solving with binary search\n";
	}
	if (m_var_lge_zero) {
		//pre_cond = pre_cond && query >= 0;
		pre_cond = pre_cond && uge(query, 0);
	}
	unsigned timeout = m_timeout;
	context &ctx = pre_cond.ctx();
	params p(ctx);
	p.set("timeout", timeout);

	unsigned sz = query.get_sort().bv_size();
	int max_bv = get_unsigned_max(sz);

	if (mode == g_max || mode == g_min_max) {
		solver sol_max(ctx);
		sol_max.add(pre_cond);
		sol_max.set(p);

		int cur_mid, cur_min = 0, cur_max = max_bv;
		expr upper = ctx.bv_val(0, sz);

		if (m_prune_unsat) {
			if (sol_max.check() == z3::sat) {
				m_binary_search_solver_call++;
				model m = sol_max.get_model();
				upper = m.eval(query);
				cur_min = upper.get_numeral_int();
			}
		}
		do {
			sol_max.push();
			cur_mid = (cur_min + cur_max) / 2;
			// cur_mid = cur_min + ((cur_max - cur_min) >> 1)
			if (m_verbose_lvl) {
				//std::cout << "min, mid, max: " << cur_min << ", " << cur_mid << ", "  <<  cur_max << std::endl;
				//std::cout << "current upper: " << upper << std::endl;
			}
			expr cur_min_expr = ctx.bv_val(cur_min, sz);
			expr cur_mid_expr = ctx.bv_val(cur_mid, sz);
			expr cur_max_expr = ctx.bv_val(cur_max, sz);
			//expr cond = query >= cur_mid_expr && query <= cur_max_expr;
			expr cond = uge(query, cur_mid_expr) && ule(query, cur_max_expr);
			sol_max.add(cond);
			check_result cond_ret = sol_max.check();
			m_binary_search_solver_call++;
			if (cond_ret == z3::unsat) {
				cur_max = cur_mid - 1;
				sol_max.pop();
			} else if (cond_ret == z3::sat) {
				model m = sol_max.get_model();
				upper = m.eval(query);
				cur_min = upper.get_numeral_int() + 1;
				sol_max.pop();
			} else {
				res.push_back(upper);
				return false;
			}
		} while (cur_min <= cur_max);
		m_binary_search_max = upper.get_numeral_int();
		res.push_back(upper);
		if (m_verbose_lvl) {
			std::cout << "  max: " << m_binary_search_max << "\n";
			std::cout << "  solver calls: " << m_binary_search_solver_call
					<< "\n";
		}
	}
	return true;
}

bool opt_solver::solve_with_compact_search(expr &pre_cond, expr_vector &queries,
		std::vector<expr_vector> &res, opt_mode mode) {
	if (m_verbose_lvl) {
		std::cout << "SymAbs: solving with compact search\n";
	}
	unsigned timeout = m_timeout;
	context &ctx = pre_cond.ctx();
	if (m_var_lge_zero) {
		for (unsigned i = 0; i < queries.size(); i++) {
			pre_cond = pre_cond && uge(queries[i], 0);
		}
	}
	params p(ctx);
	p.set("timeout", timeout);
	unsigned bvsz = queries[0].get_sort().bv_size();
	unsigned qsz = queries.size();
	int max_bv = get_unsigned_max(bvsz);

	// TODO: we need to perform the min/max search together

	if (mode == g_min || mode == g_min_max) {

	}

	if (mode == g_max || mode == g_min_max) {
		solver sol_max(ctx);
		sol_max.add(pre_cond);
		sol_max.set(p);

		std::vector<int> queries_bounds;
		std::vector<int> cur_mins;
		std::vector<int> cur_mids;
		std::vector<int> cur_maxs;
		std::vector<bool> finished;  // maybe we can use a bit-vector to label

		for (unsigned i = 0; i < qsz; i++) {
			queries_bounds.push_back(0);
			cur_mins.push_back(0);
			cur_mids.push_back(0);
			cur_maxs.push_back(max_bv);
			finished.push_back(false);
		}

# if 0
        // TODO: bound analysis needs more test ..
        if (m_compact_use_bound_ana) {
            std::vector<int> sound_max_res;
            std::vector<int> sound_min_res;
            sound_max_all(pre_cond, queries, sound_max_res);
            sound_min_all(pre_cond, queries, sound_min_res);
            for (unsigned i = 0; i < queries.size(); i++) {
                std::cout << queries[i] << " upper: " << sound_max_res[i] << std::endl;
                std::cout << queries[i] << " lower: " << sound_min_res[i] << std::endl;
            }
        }
# endif

		if (m_prune_unsat) {
			if (sol_max.check() == z3::sat) {
				m_compact_check_solver_call++;
				model m = sol_max.get_model();
				for (unsigned i = 0; i < queries.size(); i++) {
					queries_bounds[i] = m.eval(queries[i]).get_numeral_int();
					cur_mins[i] = queries_bounds[i];
					if (m_verbose_lvl) {
						std::cout << "Init min for " << queries[i] << " "
								<< cur_mins[i] << "\n";
					}
				}
			}
		}

		while (true) {
			//1. add assertions to to_compact_check
			expr_vector to_compact_check(ctx);
			for (unsigned i = 0; i < qsz; i++) {
				if (!finished[i]) {
					cur_mids[i] = (cur_mins[i] + cur_maxs[i]) / 2;
					expr cur_mid_expr = ctx.bv_val(cur_mids[i], bvsz);
					expr cur_max_expr = ctx.bv_val(cur_maxs[i], bvsz);
					expr cond = uge(queries[i], cur_mid_expr)
							&& ule(queries[i], cur_max_expr);
					to_compact_check.push_back(cond);
				} else {
					// already finished
					to_compact_check.push_back(ctx.bool_val(true));
				}
			}
			//2. perform compact check (and refinement during checking)
			std::vector<opt_solver::tri_bool> compact_res = compact_check(
					pre_cond, to_compact_check, queries, queries_bounds);

			//3. update status
			for (unsigned i = 0; i < qsz; i++) {
				if (!finished[i]) {
					opt_solver::tri_bool res_i = compact_res[i];
					if (res_i == t_true) {
						cur_mins[i] = queries_bounds[i] + 1;
					} else if (res_i == t_false) {
						cur_maxs[i] = cur_mids[i] - 1;
					} else {
						return false;
					}
				}
			}

			//4. check if some query is finished
			bool all_finished = true;
			for (unsigned i = 0; i < qsz; i++) {
				if (cur_mins[i] > cur_maxs[i]) {
					finished[i] = true;
				} else {
					all_finished = false;
				}
			}
			if (all_finished) {
				//std:cout << " All finished\n";
				//for (unsigned i = 0; i < qsz; i++) {
				//  std::cout << queries[i] << ": " << queries_bounds[i] << std::endl;
				//}
				break;
			}
		}
	}
	return true;
}

bool opt_solver::solve_with_qsmt(expr &pre_cond, expr &query, expr_vector &res,
		opt_mode mode) {
	if (m_verbose_lvl) {
		std::cout << "SymAbs: solving with qsmt\n";
	}

	unsigned timeout = m_timeout;
	context &ctx = pre_cond.ctx();
	if (m_var_lge_zero) {
		pre_cond = pre_cond && query >= 0;
	}

	params p(ctx);
	p.set("timeout", timeout);

	if (mode == g_min || mode == g_min_max) {
		// find min
		solver sol_min = tactic(ctx, "ufbv").mk_solver();
		sol_min.set(p);
		expr query_min = ctx.bv_const("rot_min", query.get_sort().bv_size());
		Z3_ast from[] = { query };
		Z3_ast to[] = { query_min };
		expr repl_min(ctx);
		repl_min = to_expr(ctx, Z3_substitute(ctx, pre_cond, 1, from, to));

		expr qsmt_min = pre_cond
				&& forall(query_min, implies(repl_min, query_min >= query));
		sol_min.add(qsmt_min);
		try {
			if (sol_min.check() == sat) {
				model m = sol_min.get_model();
				expr lower = m.eval(query);
				res.push_back(lower);
				if (m_verbose_lvl) {
					std::cout << "  min: " << lower << "\n";
				}
			} else {
				res.push_back(ctx.bool_val(false));
			}
		} catch (z3::exception &ex) {
			res.push_back(ctx.bool_val(false));
		}
	}

	if (mode == g_max || mode == g_min_max) {
		// find max
		solver sol_max = tactic(ctx, "ufbv").mk_solver();
		sol_max.set(p);
		expr query_max = ctx.bv_const("rot_max", query.get_sort().bv_size());
		Z3_ast from_x[] = { query };
		Z3_ast to_x[] = { query_max };
		expr repl_max(ctx);
		repl_max = to_expr(ctx, Z3_substitute(ctx, pre_cond, 1, from_x, to_x));
		expr qsmt_max = pre_cond
				&& forall(query_max, implies(repl_max, query_max <= query));
		sol_max.add(qsmt_max);
		if (sol_max.check() == sat) {
			model m = sol_max.get_model();
			expr upper = m.eval(query);
			res.push_back(upper);
			if (m_verbose_lvl) {
				std::cout << "  max: " << upper << "\n";
			}
		} else {
			res.push_back(ctx.bool_val(false));
		}
	}
	return true;
}

bool opt_solver::solve_with_z3opt(expr &pre_cond, expr &query, expr_vector &res,
		opt_mode mode) {
	if (m_verbose_lvl) {
		std::cout << "SymAbs: solving with z3 opt\n";
	}

	unsigned timeout = m_timeout;
	context &Ctx = pre_cond.ctx();
	if (m_var_lge_zero) {
		pre_cond = pre_cond && uge(query, 0);
	}

	params Param(Ctx);
	Param.set("priority", Ctx.str_symbol("pareto"));
	set_param("smt.timeout", (int) timeout);
	//p.set("timeout", Timeout); TODO: it seems we cannot set timeout directly to opt

	if (mode == g_min || mode == g_min_max) {
		optimize LowerFinder(Ctx);
		LowerFinder.set(Param);
		LowerFinder.add(pre_cond);
		optimize::handle LowerGoal = LowerFinder.minimize(query);
		try {
			if (LowerFinder.check() == z3::sat) {
				expr lower = LowerFinder.upper(LowerGoal);
				res.push_back(lower);
				if (m_verbose_lvl) {
					std::cout << "  min: " << lower << "\n";
				}
			}
		} catch (z3::exception &Ex) {
			res.push_back(Ctx.bool_val(false));
		}
	}

	if (mode == g_max || mode == g_min_max) {
		optimize UpperFinder(Ctx);
		UpperFinder.set(Param);
		UpperFinder.add(pre_cond);
		optimize::handle UpperGoal = UpperFinder.maximize(query);
		try {
			if (UpperFinder.check() == z3::sat) {
				expr upper = UpperFinder.lower(UpperGoal);
				res.push_back(upper);
				if (m_verbose_lvl) {
					std::cout << "  max: " << upper << "\n";
				}
			}
		} catch (z3::exception &Ex) {
			res.push_back(Ctx.bool_val(false));
		}
	}
	return true;
}

// TODO: support opt_mode ..
bool opt_solver::solve_with_z3opt(expr &pre_cond, expr_vector &query,
		std::vector<expr_vector> &res, opt_mode mode) {
	if (m_verbose_lvl) {
		std::cout << "SymAbs: solving with z3 opt\n";
	}

	unsigned timeout = m_timeout;
	context &Ctx = pre_cond.ctx();
	if (m_var_lge_zero) {
		for (unsigned i = 0; i < query.size(); i++) {
			pre_cond = pre_cond && uge(query[i], 0);
		}
	}

	params Param(Ctx);
	Param.set("priority", Ctx.str_symbol("box"));
	set_param("smt.timeout", (int) timeout);
	//p.set("timeout", Timeout); TODO: it seems we cannot set timeout directly to opt
	optimize Opt(Ctx);
	Opt.set(Param);
	Opt.add(pre_cond);

	std::vector<std::pair<optimize::handle, optimize::handle>> goals;
	for (unsigned i = 0; i < query.size(); i++) {
		goals.push_back(
				std::make_pair(Opt.minimize(query[i]), Opt.maximize(query[i])));
	}
	try {
		if (Opt.check() == z3::sat) {
			for (unsigned i = 0; i < query.size(); i++) {

				expr upper = Opt.lower(goals[i].second);
				std::cout << upper << "\n";

				res[i].push_back(Opt.upper(goals[i].first));
				res[i].push_back(Opt.lower(goals[i].second));

				if (m_verbose_lvl) {
					std::cout << query[i] << "  min : " << res[i][0]
							<< ",  max : " << res[i][1] << "\n";
				}
			}
		}
	} catch (z3::exception &Ex) {
		//res.push_back(Ctx.bool_val(false));
	}
	return true;
}

bool opt_solver::compact_check_misc(expr &G, expr_vector &FVec,
		std::vector<tri_bool> &Labels, expr_vector &queries,
		vector<int> &bounds) {
	unsigned K = FVec.size();
	expr Total = G.ctx().bool_val(false);
	for (unsigned I = 0; I < K; I++) {
		// merge formulas (with logical Or) that are not determined yet
		if (Labels[I] == t_undef) {
			Total = Total || FVec[I];
		}
	}
	if (Total.decl().decl_kind() == Z3_OP_FALSE) {
		// this indicates that no labels are l_undef (all are decided)
		return true;
	}
	Total = Total && G;
#if 0
    solver Sol(G.ctx());  //you can also first create a z3 tactic named tt and then call Sol = tt.mk_solver()
#else
	solver Sol = tactic(G.ctx(), "pp_qfbv_light").mk_solver();
	params p(G.ctx());
	p.set("enable_probing", false);
	p.set("enable_pre_simplify", false);
	p.set("burst_search", (unsigned) 150);    // default 100
	p.set("restart.initial", (unsigned) 150); // default 100
	Sol.set(p);
#endif
	Sol.add(Total);
	auto Res = Sol.check();
	m_compact_check_solver_call++;
	if (Res == z3::unsat) {
		// The over-approximated formula is UNSAT, so the remaining ones are all t_false
		for (unsigned I = 0; I < K; I++) {
			if (Labels[I] == t_undef) {
				Labels[I] = t_false;
			}
		}
		return true;
	} else if (Res == z3::sat) {
		// Use model-based filtering
		model M = Sol.get_model();
		for (unsigned I = 0; I < K; I++) {
			if (Labels[I] == t_undef) {
				auto TmpRes = M.eval(FVec[I], true);
				if (TmpRes.decl().decl_kind() == Z3_OP_TRUE) {
					// true under this model
					Labels[I] = t_true;

					// update the bounds info.
					expr val_query_i = M.eval(queries[I]);
					if (val_query_i.get_numeral_int() >= bounds[I]) {
						bounds[I] = val_query_i.get_numeral_int();
					}
				}
			}
		}
		compact_check_misc(G, FVec, Labels, queries, bounds);
	} else {
		// If any call to solver times out, the remaining labels are L_Undef. This is not a good idea..
		return false;
	}
}

// G is the pre-conditioned constraint, and FVec contains F1,...,Fn
// the goal is to check if each G \land Fi is satisfiable or not.
std::vector<opt_solver::tri_bool> opt_solver::compact_check(expr &G,
		expr_vector &FVec, expr_vector &queries, vector<int> &bounds) {
	std::vector<tri_bool> Labels;
	for (unsigned I = 0; I < FVec.size(); I++) {
		if (FVec[I].is_true()) {
			Labels.push_back(t_true);
		} else {
			Labels.push_back(t_undef);
		}
	}
	compact_check_misc(G, FVec, Labels, queries, bounds);
	return Labels;
}

void opt_solver::sound_max_all(expr &pre_cond, expr_vector &queries,
		std::vector<int> &res) {
	context &ctx = pre_cond.ctx();
	tactic get_bound = tactic(ctx, "simplify")
			& tactic(ctx, "bv-bounds-collect");
	goal g(ctx);
	g.add(pre_cond);
	goal bound = get_bound.apply(g)[0];

	for (unsigned i = 0; i < queries.size(); i++) {
		res.push_back(-INT_MAX);
	}

	for (unsigned i = 0; i < bound.size(); i++) {
		if (bound[i].decl().decl_kind() == Z3_OP_SLEQ) {
			string lhs_name = bound[i].arg(0).decl().name().str();
			for (unsigned j = 0; j < queries.size(); j++) {
				if (lhs_name == queries[j].decl().name().str()) {
					res[j] = bound[i].arg(1).get_numeral_int();
				}
			}
		} else if (bound[i].decl().decl_kind() == Z3_OP_ULEQ) {
			string lhs_name = bound[i].arg(0).decl().name().str();
			for (unsigned j = 0; j < queries.size(); j++) {
				if (lhs_name == queries[j].decl().name().str()) {
					res[j] = bound[i].arg(1).get_numeral_int();
				}
			}
		}
	}
}

void opt_solver::sound_min_all(expr &pre_cond, expr_vector &queries,
		std::vector<int> &res) {
	context &ctx = pre_cond.ctx();
	tactic get_bound = tactic(ctx, "simplify") & tactic(ctx, "propagate-values")
			& tactic(ctx, "bv-bounds-collect");
	goal g(ctx);
	g.add(pre_cond);
	goal bound = get_bound.apply(g)[0];

	for (unsigned i = 0; i < queries.size(); i++) {
		res.push_back(-INT_MAX);
	}

	for (unsigned i = 0; i < bound.size(); i++) {
		if (bound[i].decl().decl_kind() == Z3_OP_SLEQ) {
			string rhs_name = bound[i].arg(1).decl().name().str();
			for (unsigned j = 0; j < queries.size(); j++) {
				if (rhs_name == queries[j].decl().name().str()) {
					res[j] = bound[i].arg(0).get_numeral_int();
				}
			}
		} else if (bound[i].decl().decl_kind() == Z3_OP_ULEQ) {
			string rhs_name = bound[i].arg(1).decl().name().str();
			for (unsigned j = 0; j < queries.size(); j++) {
				if (rhs_name == queries[j].decl().name().str()) {
					res[j] = bound[i].arg(0).get_numeral_int();
				}
			}
		}
	}
}

