#ifndef _hpylm_
#define _hpylm_
#include <vector>
#include <random>
#include <unordered_map> 
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include "c_printf.h"
#include "sampler.h"
#include "node.h"
#include "const.h"
#include "vocab.h"

class HPYLM{
private:
	friend class boost::serialization::access;
	template <class Archive>
	// モデルの保存
	void serialize(Archive& archive, unsigned int version)
	{
		static_cast<void>(version); // No use
		archive & _root;
		archive & _max_depth;
		archive & _g0;
		archive & _d_m;
		archive & _theta_m;
		archive & _a_m;
		archive & _b_m;
		archive & _alpha_m;
		archive & _beta_m;
	}
public:
	Node* _root;				// 文脈木のルートノード
	int _max_depth;				// 最大の深さ
	int _bottom;				// VPYLMへ拡張時に使う
	double _g0;					// ゼログラム確率

	// 深さmのノードに関するパラメータ
	vector<double> _d_m;		// Pitman-Yor過程のディスカウント係数
	vector<double> _theta_m;	// Pitman-Yor過程の集中度

	// "A Bayesian Interpretation of Interpolated Kneser-Ney" Appendix C参照
	// http://www.gatsby.ucl.ac.uk/~ywteh/research/compling/hpylm.pdf
	vector<double> _a_m;		// ベータ分布のパラメータ	dの推定用
	vector<double> _b_m;		// ベータ分布のパラメータ	dの推定用
	vector<double> _alpha_m;	// ガンマ分布のパラメータ	θの推定用
	vector<double> _beta_m;		// ガンマ分布のパラメータ	θの推定用

	HPYLM(int ngram = 2){
		// 深さは0から始まることに注意
		// バイグラムなら最大深さは1
		_max_depth = ngram - 1;
		_bottom = -1;

		_root = new Node(0);
		_root->_depth = 0;	// ルートは深さ0

		for(int n = 0;n < ngram;n++){
			_d_m.push_back(PYLM_INITIAL_D);	
			_theta_m.push_back(PYLM_INITIAL_THETA);
			_a_m.push_back(PYLM_INITIAL_A);	
			_b_m.push_back(PYLM_INITIAL_B);	
			_alpha_m.push_back(PYLM_INITIAL_ALPHA);
			_beta_m.push_back(PYLM_INITIAL_BETA);
		}
	}
	// 単語列のindex番目の単語をモデルに追加
	bool add_customer_at_timestep(vector<id> &token_ids, int token_t_index){
		Node* node = find_node_by_tracing_back_context(token_ids, token_t_index, true);
		if(node == NULL){
			c_printf("[R]%s", "エラー");
			c_printf("[n]%s\n", " 客を追加できません. ノードが見つかりません.");
			return false;
		}
		id token_t = token_ids[token_t_index];
		node->add_customer(token_t, _g0, _d_m, _theta_m);
		return false;
	}
	bool remove_customer_at_timestep(vector<id> &token_ids, int token_t_index){
		Node* node = find_node_by_tracing_back_context(token_ids, token_t_index, false);
		if(node == NULL){
			c_printf("[R]%s", "エラー");
			c_printf("[n]%s\n", " 客を除去できません. ノードが見つかりません.");
			return false;
		}
		id token_t = token_ids[token_t_index];
		node->remove_customer(token_t);
		// 客が一人もいなくなったらノードを削除する
		if(node->need_to_remove_from_parent()){
			node->remove_from_parent();
		}
		return true;
	}
	// 文脈を後ろ向きに_max_depthだけ辿る
	Node* find_node_by_tracing_back_context(vector<id> &token_ids, int token_t_index, bool generate_node_if_needed = false){
		// HPYLMでは深さは固定
		if(token_t_index < _max_depth){
			return NULL;
		}
		Node* node = _root;
		for(int depth = 0;depth < _max_depth;depth++){
			id context_token_id = token_ids[token_t_index - depth - 1];
			Node* child = node->find_child_node(context_token_id, generate_node_if_needed);
			if(child == NULL){
				return NULL;
			}
			node = child;
		}
		return node;
	}
	double Pw_h(vector<id> &token_ids, vector<id> context_token_ids){
		double p = 1;
		for(int n = 0;n < token_ids.size();n++){
			p *= Pw_h(token_ids[n], context_token_ids);
			context_token_ids.push_back(token_ids[n]);
		}
		return p;
	}
	double Pw_h(id token_id, vector<id> &context_token_ids){
		// HPYLMでは深さは固定
		if(context_token_ids.size() < _max_depth){
			c_printf("[R]%s", "エラー");
			c_printf("[n]%s\n", " 単語確率を計算できません. context_token_ids.size() < _max_depth");
			return -1;
		}
		Node* node = find_node_by_tracing_back_context(context_token_ids, token_id, false);
		if(node == NULL){
			c_printf("[R]%s", "エラー");
			c_printf("[n]%s\n", " 単語確率を計算できません. node == NULL");
			return -1;
		}
		return node->Pw(token_id, _g0, _d_m, _theta_m);
	}
	double Pw(id token_id){
		return _root->Pw(token_id, _g0, _d_m, _theta_m);
	}
	double Pw(vector<id> &token_ids){
		if(token_ids.size() < _max_depth + 1){
			c_printf("[R]%s", "エラー");
			c_printf("[n]%s\n", " 単語確率を計算できません. token_ids.size() < _max_depth");
			return -1;
		}
		double mul_Pw_h = 1;
		vector<id> context_token_ids(token_ids.begin(), token_ids.begin() + _max_depth);
		for(int depth = _max_depth;depth < token_ids.size();depth++){
			id token_id = token_ids[depth];
			mul_Pw_h *= Pw_h(token_id, context_token_ids);;
			context_token_ids.push_back(token_id);
		}
		return mul_Pw_h;
	}
	double log_Pw(vector<id> &token_ids){
		if(token_ids.size() < _max_depth + 1){
			c_printf("[R]%s", "エラー");
			c_printf("[n]%s\n", " 単語確率を計算できません. token_ids.size() < _max_depth");
			return -1;
		}
		double sum_Pw_h = 0;
		vector<id> context_token_ids(token_ids.begin(), token_ids.begin() + _max_depth);
		for(int depth = _max_depth;depth < token_ids.size();depth++){
			id token_id = token_ids[depth];
			double prob = Pw_h(token_id, context_token_ids);
			sum_Pw_h += log(prob + 1e-10);
			context_token_ids.push_back(token_id);
		}
		return sum_Pw_h;
	}
	double log2_Pw(vector<id> &token_ids){
		if(token_ids.size() < _max_depth + 1){
			c_printf("[R]%s", "エラー");
			c_printf("[n]%s\n", " 単語確率を計算できません. token_ids.size() < _max_depth");
			return -1;
		}
		double sum_Pw_h = 0;
		vector<id> context_token_ids(token_ids.begin(), token_ids.begin() + _max_depth);
		for(int depth = _max_depth;depth < token_ids.size();depth++){
			id token_id = token_ids[depth];
			double prob = Pw_h(token_id, context_token_ids);
			sum_Pw_h += log2(prob + 1e-10);
			context_token_ids.push_back(token_id);
		}
		return sum_Pw_h;
	}
	id sample_next_token(vector<id> &context_token_ids, id eos_id){
		Node* node = find_node_by_tracing_back_context(context_token_ids, context_token_ids.size() - 1, false);
		if(node == NULL){
			c_printf("[R]%s", "エラー");
			c_printf("[n]%s\n", " トークンを生成できません. ノードが見つかりません.");
			return eos_id;
		}
		vector<id> token_ids;
		vector<double> probs;
		double sum_probs = 0;
		for(auto elem: node->_arrangement){
			id token_id = elem.first;
			double prob = Pw_h(token_id, context_token_ids);
			if(prob > 0){
				token_ids.push_back(token_id);
				probs.push_back(prob);
				sum_probs += prob;
			}
		}
		if(token_ids.size() == 0){
			return eos_id;
		}
		if(sum_probs == 0){
			return eos_id;
		}
		double ratio = 1.0 / sum_probs;
		double r = Sampler::uniform(0, 1);
		sum_probs = 0;
		id sampled_token_id = token_ids.back();
		for(int i = 0;i < token_ids.size();i++){
			sum_probs += probs[i] * ratio;
			if(sum_probs > r){
				return token_ids[i];
			}
		}
		return sampled_token_id;
	}
	void init_hyperparameters_at_depth_if_needed(int depth){
		if(depth >= _d_m.size()){
			while(_d_m.size() <= depth){
				_d_m.push_back(PYLM_INITIAL_D);
			}
		}
		if(depth >= _theta_m.size()){
			while(_theta_m.size() <= depth){
				_theta_m.push_back(PYLM_INITIAL_THETA);
			}
		}
		if(depth >= _a_m.size()){
			while(_a_m.size() <= depth){
				_a_m.push_back(PYLM_INITIAL_A);
			}
		}
		if(depth >= _b_m.size()){
			while(_b_m.size() <= depth){
				_b_m.push_back(PYLM_INITIAL_B);
			}
		}
		if(depth >= _alpha_m.size()){
			while(_alpha_m.size() <= depth){
				_alpha_m.push_back(PYLM_INITIAL_ALPHA);
			}
		}
		if(depth >= _beta_m.size()){
			while(_beta_m.size() <= depth){
				_beta_m.push_back(PYLM_INITIAL_BETA);
			}
		}
	}
	// "A Bayesian Interpretation of Interpolated Kneser-Ney" Appendix C参照
	// http://www.gatsby.ucl.ac.uk/~ywteh/research/compling/hpylm.pdf
	void sum_auxiliary_variables_recursively(Node* node, vector<double> &sum_log_x_u_m, vector<double> &sum_y_ui_m, vector<double> &sum_1_y_ui_m, vector<double> &sum_1_z_uwkj_m, int &bottom){
		for(auto elem: node->_children){
			Node* child = elem.second;
			int depth = child->_depth;

			if(depth > bottom){
				bottom = depth;
			}
			init_hyperparameters_at_depth_if_needed(depth);

			double d = _d_m[depth];
			double theta = _theta_m[depth];
			sum_log_x_u_m[depth] += child->auxiliary_log_x_u(theta);	// log(x_u)
			sum_y_ui_m[depth] += child->auxiliary_y_ui(d, theta);		// y_ui
			sum_1_y_ui_m[depth] += child->auxiliary_1_y_ui(d, theta);	// 1 - y_ui
			sum_1_z_uwkj_m[depth] += child->auxiliary_1_z_uwkj(d);		// 1 - z_uwkj

			sum_auxiliary_variables_recursively(child, sum_log_x_u_m, sum_y_ui_m, sum_1_y_ui_m, sum_1_z_uwkj_m, _bottom);
		}
	}
	// dとθの推定
	void sample_hyperparams(){
		int max_depth = _d_m.size() - 1;

		// 親ノードの深さが0であることに注意
		vector<double> sum_log_x_u_m(max_depth + 1, 0.0);
		vector<double> sum_y_ui_m(max_depth + 1, 0.0);
		vector<double> sum_1_y_ui_m(max_depth + 1, 0.0);
		vector<double> sum_1_z_uwkj_m(max_depth + 1, 0.0);

		// _root
		sum_log_x_u_m[0] = _root->auxiliary_log_x_u(_theta_m[0]);			// log(x_u)
		sum_y_ui_m[0] = _root->auxiliary_y_ui(_d_m[0], _theta_m[0]);		// y_ui
		sum_1_y_ui_m[0] = _root->auxiliary_1_y_ui(_d_m[0], _theta_m[0]);	// 1 - y_ui
		sum_1_z_uwkj_m[0] = _root->auxiliary_1_z_uwkj(_d_m[0]);				// 1 - z_uwkj

		// それ以外
		_bottom = 0;
		// _bottomは以下を実行すると更新される
		sum_auxiliary_variables_recursively(_root, sum_log_x_u_m, sum_y_ui_m, sum_1_y_ui_m, sum_1_z_uwkj_m, _bottom);
		init_hyperparameters_at_depth_if_needed(_bottom);

		for(int u = 0;u <= _bottom;u++){
			_d_m[u] = Sampler::beta(_a_m[u] + sum_1_y_ui_m[u], _b_m[u] + sum_1_z_uwkj_m[u]);
			// 2番目の引数は逆数を渡すことに注意
			_theta_m[u] = Sampler::gamma(_alpha_m[u] + sum_y_ui_m[u], 1 / (_beta_m[u] - sum_log_x_u_m[u]));
		}
		// 不要な深さのハイパーパラメータを削除
		int num_remove = _d_m.size() - _bottom - 1;
		for(int n = 0;n < num_remove;n++){
			_d_m.pop_back();
			_theta_m.pop_back();
			_a_m.pop_back();
			_b_m.pop_back();
			_alpha_m.pop_back();
			_beta_m.pop_back();
		}
	}
	int get_max_depth(bool use_cache = true){
		if(use_cache && _bottom != -1){
			return _bottom;
		}
		update_max_depth_recursively(_root);
		return _bottom;
	}
	void update_max_depth_recursively(Node* node){
		for(auto elem: node->_children){
			Node* child = elem.second;
			int depth = child->_depth;
			if(depth > _bottom){
				_bottom = depth;
			}
			update_max_depth_recursively(child);
		}
	}
	int get_num_child_nodes(){
		return _root->get_num_child_nodes();
	}
	int get_num_customers(){
		return _root->get_num_customers();
	}
	void set_active_tokens(unordered_map<id, bool> &flags){
		_root->set_active_tokens(flags);
	}
	void count_node_of_each_depth(unordered_map<id, int> &map){
		_root->count_node_of_each_depth(map);
	}
	void save(string dir = "model/"){
		string filename = dir + "hpylm.model";
		std::ofstream ofs(filename);
		boost::archive::binary_oarchive oarchive(ofs);
		oarchive << static_cast<const HPYLM&>(*this);
		cout << "saved to " << filename << endl;
		cout << "	num_customers: " << get_num_customers() << endl;
		cout << "	num_nodes: " << get_num_child_nodes() << endl;
		cout << "	max_depth: " << get_max_depth() << endl;
	}
	void load(string dir = "model/"){
		string filename = dir + "hpylm.model";
		std::ifstream ifs(filename);
		if(ifs.good()){
			boost::archive::binary_iarchive iarchive(ifs);
			iarchive >> *this;
		}
	}
};

#endif