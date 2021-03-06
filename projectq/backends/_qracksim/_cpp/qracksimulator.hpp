// Copyright 2017-2019 ProjectQ-Framework (www.projectq.ch)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef QRACK_SIMULATOR_HPP_
#define QRACK_SIMULATOR_HPP_

#include "qrack/qfactory.hpp"
#include "qrack/common/config.h"

#include <vector>
#include <complex>

#include "intrin/alignedallocator.hpp"
#include <map>
#include <cassert>
#include <algorithm>
#include <tuple>
#include <random>
#include <functional>
#include <string>

#if defined(_OPENMP)
#include <omp.h>
#endif

#define CREATE_QUBITS(count) Qrack::CreateQuantumInterface(QrackEngine, QrackSubengine, count, 0, rnd_eng_, Qrack::ONE_CMPLX, false, false, false, devID, true)

class QrackSimulator{
public:
    using calc_type = Qrack::real1;
    using complex_type = Qrack::complex;
    using StateVector = std::vector<std::complex<calc_type>, aligned_allocator<std::complex<calc_type>,64>>;
    using Map = std::map<unsigned, unsigned>;
    using RndEngine = qrack_rand_gen;
    using Term = std::vector<std::pair<unsigned, char>>;
    using TermsDict = std::vector<std::pair<Term, calc_type>>;
    using ComplexTermsDict = std::vector<std::pair<Term, std::complex<calc_type>>>;
    using Matrix = std::vector<std::vector<std::complex<double>, aligned_allocator<std::complex<double>, 64>>>;
    enum Qrack::QInterfaceEngine QrackEngine = Qrack::QINTERFACE_QUNIT;
    enum Qrack::QInterfaceEngine QrackSubengine = Qrack::QINTERFACE_OPTIMAL;
    typedef std::function<void(bitLenInt*, bitLenInt, bitLenInt, calc_type*)> UCRFunc;
    typedef std::function<void(bitLenInt, bitLenInt, bitLenInt*, bitLenInt)> CINTFunc;
    typedef std::function<void(bitLenInt, bitLenInt, bitLenInt, bitLenInt*, bitLenInt)> CMULXFunc;
    int devID;

    QrackSimulator(unsigned seed = 1, const int& dev = -1, const int& simulator_type = 1, const bool& build_from_source = false, const bool& save_binaries = false, std::string cache_path = "*")
        :qReg(NULL) {
        rnd_eng_ = std::make_shared<RndEngine>();
    	rnd_eng_->seed(seed);
        devID = dev;

#if ENABLE_OPENCL
        // Initialize OpenCL engine, and set the default device context.
        Qrack::OCLEngine::InitOCL(build_from_source, save_binaries, cache_path);
#endif
        if (simulator_type == 3) {
            QrackEngine = Qrack::QINTERFACE_QUNIT_MULTI;
            QrackSubengine = Qrack::QINTERFACE_OPTIMAL;
        } else if (simulator_type == 1) {
            QrackEngine = Qrack::QINTERFACE_QUNIT;
            QrackSubengine = Qrack::QINTERFACE_OPTIMAL;
        } else {
            QrackEngine = Qrack::QINTERFACE_OPTIMAL;
            QrackSubengine = Qrack::QINTERFACE_OPTIMAL;
        }
    }

    void allocate_qubit(unsigned id){
        if (map_.count(id) == 0) {
            if (qReg == NULL) {
                map_[id] = 0;
                qReg = CREATE_QUBITS(1); 
            } else {
                map_[id] = qReg->Compose(CREATE_QUBITS(1));
            }
        }
        else
            throw(std::runtime_error(
                "AllocateQubit: ID already exists. Qubit IDs should be unique."));
    }

    bool get_classical_value(unsigned id, calc_type tol = min_norm){
        if (qReg->Prob(map_[id]) < 0.5) {
            return false;
        } else {
            return true;
        }
    }

    bool is_classical(unsigned id, calc_type tol = min_norm){
        calc_type p = qReg->Prob(map_[id]);
        if ((p < tol) || ((ONE_R1 - p) < tol)) {
            return true;
        } else {
            return false;
        }
    }

    void measure_qubits(std::vector<unsigned> const& ids, std::vector<bool> &res){
        bitLenInt i;
        bitLenInt* bits = new bitLenInt[ids.size()];
        for (i = 0; i < ids.size(); i++) {
            bits[i] = map_[ids[i]];
        }
        bitCapInt allRes = qReg->M(bits, ids.size());
        res.resize(ids.size());
        for (i = 0; i < ids.size(); i++) {
            res[i] = !(!(allRes & (1U << bits[i])));
        }
        delete[] bits;
    }

    std::vector<bool> measure_qubits_return(std::vector<unsigned> const& ids){
        std::vector<bool> ret;
        measure_qubits(ids, ret);
        return ret;
    }

    void deallocate_qubit(unsigned id){
        if (map_.count(id) == 0)
            throw(std::runtime_error("Error: No qubit with given ID, to deallocate."));
        if (!is_classical(id))
            throw(std::runtime_error("Error: Qubit has not been measured / uncomputed! There is most likely a bug in your code."));

        if (qReg->GetQubitCount() == 1) {
            qReg = NULL;
        } else {
            qReg->Dispose(map_[id], 1U);
        }

        bitLenInt mapped = map_[id];
        map_.erase(id);

        Map::iterator it;
        for (it = map_.begin(); it != map_.end(); it++) {
            if (mapped < (it->second)) {
                it->second--;
            }
        }
    }

    template <class M>
    void apply_controlled_gate(M const& m, std::vector<unsigned> ids,
                               std::vector<unsigned> ctrl){
        complex_type mArray[4] = {
            complex_type(real(m[0][0]), imag(m[0][0])), complex_type(real(m[0][1]), imag(m[0][1])),
            complex_type(real(m[1][0]), imag(m[1][0])), complex_type(real(m[1][1]), imag(m[1][1]))
        };

        if (ctrl.size() == 0) {
            for (bitLenInt i = 0; i < ids.size(); i++) {
                qReg->ApplySingleBit(mArray, map_[ids[i]]);
            }
            return;
        }

        bitLenInt* ctrlArray = new bitLenInt[ctrl.size()];
        for (bitLenInt i = 0; i < ctrl.size(); i++) {
            ctrlArray[i] = map_[ctrl[i]];
        }

        for (bitLenInt i = 0; i < ids.size(); i++) {
            qReg->ApplyControlledSingleBit(ctrlArray, ctrl.size(), map_[ids[i]], mArray);
        }

        delete[] ctrlArray;
    }

    void apply_controlled_swap(std::vector<unsigned> ids1,
                               std::vector<unsigned> ids2,
                               std::vector<unsigned> ctrl){

        assert(ids1.size() == ids2.size());

        if (ctrl.size() == 0) {
            for (bitLenInt i = 0; i < ids1.size(); i++) {
                qReg->Swap(map_[ids1[i]], map_[ids2[i]]);
            }
            return;
        }

        bitLenInt* ctrlArray = new bitLenInt[ctrl.size()];
        for (bitLenInt i = 0; i < ctrl.size(); i++) {
            ctrlArray[i] = map_[ctrl[i]];
        }

        for (bitLenInt i = 0; i < ids1.size(); i++) {
            qReg->CSwap(ctrlArray, ctrl.size(), map_[ids1[i]], map_[ids2[i]]);
        }

        delete[] ctrlArray;
    }

    void apply_controlled_sqrtswap(std::vector<unsigned> ids1,
                               std::vector<unsigned> ids2,
                               std::vector<unsigned> ctrl){

        assert(ids1.size() == ids2.size());

        if (ctrl.size() == 0) {
            for (bitLenInt i = 0; i < ids1.size(); i++) {
                qReg->SqrtSwap(map_[ids1[i]], map_[ids2[i]]);
            }
            return;
        }

        bitLenInt* ctrlArray = new bitLenInt[ctrl.size()];
        for (bitLenInt i = 0; i < ctrl.size(); i++) {
            ctrlArray[i] = map_[ctrl[i]];
        }

        for (bitLenInt i = 0; i < ids1.size(); i++) {
            qReg->CSqrtSwap(ctrlArray, ctrl.size(), map_[ids1[i]], map_[ids2[i]]);
        }

        delete[] ctrlArray;
    }

    void apply_controlled_phase_gate(calc_type angle, std::vector<unsigned> ctrl){
        calc_type cosine = cos(angle);
        calc_type sine = sin(angle);

        complex_type mArray[4] = {
            complex_type(cosine, sine), complex_type(ZERO_R1, ZERO_R1),
            complex_type(ZERO_R1, ZERO_R1), complex_type(cosine, sine)
        };

        if (ctrl.size() == 0) {
            qReg->ApplySingleBit(mArray, 0);
            return;
        }

        bitLenInt* ctrlArray = new bitLenInt[ctrl.size()];
        for (bitLenInt i = 0; i < ctrl.size(); i++) {
            ctrlArray[i] = map_[ctrl[i]];
        }

        bitLenInt target = 0;
        while(std::find(ctrlArray, ctrlArray + ctrl.size(), target) != (ctrlArray + ctrl.size())) {
            target++;
        }

        qReg->ApplyControlledSingleBit(ctrlArray, ctrl.size(), target, mArray);

        delete[] ctrlArray;
    }

    void apply_uniformly_controlled_ry(std::vector<calc_type> angles, std::vector<unsigned> ids, std::vector<unsigned> ctrl){
        apply_uniformly_controlled(angles, ids, ctrl, [&](bitLenInt* ctrlArray, bitLenInt controlLen, bitLenInt trgt, calc_type* anglesArray) {
            qReg->UniformlyControlledRY(ctrlArray, controlLen, trgt, anglesArray);
        });
    }

    void apply_uniformly_controlled_rz(std::vector<calc_type> angles, std::vector<unsigned> ids, std::vector<unsigned> ctrl){
        apply_uniformly_controlled(angles, ids, ctrl, [&](bitLenInt* ctrlArray, bitLenInt controlLen, bitLenInt trgt, calc_type* anglesArray) {
            qReg->UniformlyControlledRZ(ctrlArray, controlLen, trgt, anglesArray);
        });
    }

    void apply_controlled_inc(std::vector<unsigned> ids, std::vector<unsigned> ctrl, bitCapInt toAdd){
        apply_controlled_int([&](bitLenInt start, bitLenInt size, bitLenInt* ctrlArray, bitLenInt ctrlSize) {
            qReg->CINC(toAdd, start, size, ctrlArray, ctrlSize);
        }, ids, ctrl);
    }

    void apply_controlled_dec(std::vector<unsigned> ids, std::vector<unsigned> ctrl, bitCapInt toSub){
        apply_controlled_int([&](bitLenInt start, bitLenInt size, bitLenInt* ctrlArray, bitLenInt ctrlSize) {
            qReg->CDEC(toSub, start, size, ctrlArray, ctrlSize);
        }, ids, ctrl);
    }

    void apply_controlled_mul(std::vector<unsigned> ids, std::vector<unsigned> ctrl, bitCapInt toMul){
        apply_controlled_mulx([&](bitLenInt start, bitLenInt carryStart, bitLenInt size, bitLenInt* ctrlArray, bitLenInt ctrlSize) {
            qReg->CMUL(toMul, start, carryStart, size, ctrlArray, ctrlSize);
        }, ids, ctrl);
    }

    void apply_controlled_div(std::vector<unsigned> ids, std::vector<unsigned> ctrl, bitCapInt toDiv){
        apply_controlled_mulx([&](bitLenInt start, bitLenInt carryStart, bitLenInt size, bitLenInt* ctrlArray, bitLenInt ctrlSize) {
            qReg->CDIV(toDiv, start, carryStart, size, ctrlArray, ctrlSize);
        }, ids, ctrl);
    }

    calc_type get_probability(std::vector<bool> const& bit_string,
                              std::vector<unsigned> const& ids){
        if (!check_ids(ids))
            throw(std::runtime_error("get_probability(): Unknown qubit id."));

        if (ids.size() == 1) {
            // If we're checking a single bit, this is significantly faster.
            if (bit_string[0]) {
                return qReg->Prob(map_[ids[0]]);
            } else {
                return (ONE_R1 - qReg->Prob(map_[ids[0]]));
            }
        }

        bitCapInt mask = 0, bit_str = 0;
        for (bitLenInt i = 0; i < ids.size(); i++){
            mask |= Qrack::pow2(map_[ids[i]]);
            bit_str |= bit_string[i] ? Qrack::pow2(map_[ids[i]]) : 0;
        }
        return qReg->ProbMask(mask, bit_str);
    }

    std::complex<calc_type> get_amplitude(std::vector<bool> const& bit_string,
                                      std::vector<unsigned> const& ids){
        bitCapInt chk = 0;
        bitCapInt index = 0;
        for (bitLenInt i = 0; i < ids.size(); i++){
            if (map_.count(ids[i]) == 0)
                break;
            chk |= Qrack::pow2(map_[ids[i]]);
            index |= bit_string[i] ? Qrack::pow2(map_[ids[i]]) : 0;
        }
        if ((chk + 1U) != qReg->GetMaxQPower())
            throw(std::runtime_error("The second argument to get_amplitude() must be a permutation of all allocated qubits. Please make sure you have called eng.flush()."));
        complex_type result = qReg->GetAmplitude(index);
        return std::complex<calc_type>(real(result), imag(result));
    }

    void emulate_time_evolution(TermsDict const& tdict, calc_type const& time,
                                std::vector<unsigned> const& ids,
                                std::vector<unsigned> const& ctrl){
        bitLenInt* ctrlArray = NULL;
        if (ctrl.size() > 0) {
            ctrlArray = new bitLenInt[ctrl.size()];
            for (bitLenInt i = 0; i < ctrl.size(); i++) {
                ctrlArray[i] = map_[ctrl[i]];
            }
        }

        std::complex<double> I(0., 1.);
        Matrix X = {{0., 1.}, {1., 0.}};
        Matrix Y = {{0., -I}, {I, 0.}};
        Matrix Z = {{1., 0.}, {0., -1.}};
        std::vector<Matrix> gates = {X, Y, Z};
        std::map<unsigned, Qrack::BitOp> collectedTerms;

        for (auto const& term : tdict){
            for (auto const& local_op : term.first){
                unsigned id = map_[ids[local_op.first]];
                auto temp = gates[local_op.second - 'X'];
                for (unsigned i = 0; i < 4; i++) {
                    temp[i / 2][i % 2] *= term.second;
                }
                if (collectedTerms.find(id) == collectedTerms.end()) {
                    Qrack::BitOp op(new Qrack::complex[4], std::default_delete<Qrack::complex[]>());
                    for (unsigned i = 0; i < 4; i++) {
                        op.get()[i] = Qrack::complex(real(temp[i / 2][i % 2]), imag(temp[i / 2][i % 2]));
                    }
                    collectedTerms[id] = op;
                } else {
                    for (unsigned i = 0; i < 4; i++) {
                        collectedTerms[id].get()[i] += Qrack::complex(real(temp[i / 2][i % 2]), imag(temp[i / 2][i % 2]));
                    }
                }
            }
        }

        Qrack::Hamiltonian hamiltonian(collectedTerms.size());
        std::map<unsigned, Qrack::BitOp>::iterator cTI;
        unsigned index = 0;
        for (cTI = collectedTerms.begin(); cTI != collectedTerms.end(); cTI++) {
            if (ctrl.size() > 0) {
                hamiltonian[index] = std::make_shared<Qrack::HamiltonianOp>(ctrlArray, ctrl.size(), cTI->first, cTI->second);
            } else {
                hamiltonian[index] = std::make_shared<Qrack::HamiltonianOp>(cTI->first, cTI->second);
            }
            index++;
        }

        qReg->TimeEvolve(hamiltonian, time);

        if (ctrl.size() > 0) {
            delete[] ctrlArray;
        }
    }

    void set_wavefunction(StateVector const& wavefunction, std::vector<unsigned> const& ordering){
        // make sure there are 2^n amplitudes for n qubits
        assert(wavefunction.size() == (1UL << ordering.size()));
        // check that all qubits have been allocated previously
        if (map_.size() != ordering.size() || !check_ids(ordering))
            throw(std::runtime_error("set_wavefunction(): Invalid mapping provided. Please make sure all qubits have been allocated previously."));

        // set mapping and wavefunction
        for (unsigned i = 0; i < ordering.size(); i++)
            map_[ordering[i]] = i;

        qReg->SetQuantumState(&(wavefunction[0]));
    }

    void collapse_wavefunction(std::vector<unsigned> const& ids, std::vector<bool> const& values){
        assert(ids.size() == values.size());
        if (!check_ids(ids))
            throw(std::runtime_error("collapse_wavefunction(): Unknown qubit id(s) provided. Try calling eng.flush() before invoking this function."));
        bitCapInt mask = 0, val = 0;
        bitLenInt* idsArray = new bitLenInt[ids.size()];
        bool* valuesArray = new bool[values.size()];
        for (bitLenInt i = 0; i < ids.size(); i++){
            idsArray[i] = map_[ids[i]];
            mask |= (1UL << map_[ids[i]]);
            val |= ((values[i]?1UL:0UL) << map_[ids[i]]);
            valuesArray[i] = values[i];
        }
        calc_type N = qReg->ProbMask(mask, val);
        if (N < min_norm)
            throw(std::runtime_error("collapse_wavefunction(): Invalid collapse! Probability is ~0."));

        qReg->ForceM(idsArray, ids.size(), valuesArray);

        delete[] valuesArray;
        delete[] idsArray;
    }

    void prepare_state(std::vector<unsigned> const& ids, std::vector<std::complex<calc_type>> const& amps){
        // We can prepare arbitrary substates with measurement, "Compose," and "Decompose.
        assert((1U << ids.size()) == amps.size());

        // If the substate being prepared is the full set, then set the amplitudes, and we're done.
        if (ids.size() == qReg->GetQubitCount()) {
            qReg->SetQuantumState(&(amps[0]));
            return;
        }

        // Otherwise, this is a subset less than the full set of qubits.

        // First, collapse the old substate and throw it away.
        bitLenInt mapped;
        for (bitLenInt i = 0; i < ids.size(); i++) {
            qReg->M(map_[ids[i]]);
            mapped = map_[ids[i]];
            qReg->Dispose(mapped, 1);
            map_.erase(ids[i]);
            for (Map::iterator it = map_.begin(); it != map_.end(); it++) {
                if (it->second > mapped) {
                    it->second--;
                }
            }
        }

        // Then, prepare the new substate.
        Qrack::QInterfacePtr substate = CREATE_QUBITS(ids.size());
        substate->SetQuantumState(&(amps[0]));

        // Finally, combine the representation of the new substate with the remainder of the old engine.
        bitLenInt oldLength = qReg->Compose(substate);

        for (bitLenInt i = 0; i < ids.size(); i++) {
            map_[ids[i]] = oldLength + i;
        }
    }

    void apply_qubit_operator(ComplexTermsDict const& td, std::vector<unsigned> const& ids){
        for (auto const& term : td){
            apply_term(term.first, term.second, ids, {});
        }
    }

    calc_type get_expectation_value(TermsDict const& td, std::vector<unsigned> const& ids){
        calc_type expectation = 0;

        std::size_t mask = 0;
        for (unsigned i = 0; i < ids.size(); i++){
            mask |= 1UL << map_[ids[i]];
        }

        run();

        Qrack::QInterfacePtr qRegOrig = qReg->Clone();
        for (auto const& term : td){
            expectation += diagonalize(term.first, term.second, ids);
            qReg = qRegOrig->Clone();
        }

        return expectation;
    }

    std::tuple<Map, StateVector> cheat(){
        if (qReg == NULL) {
            StateVector vec(1, 0.0);
            return make_tuple(map_, std::move(vec));
        }

        StateVector vec(qReg->GetMaxQPower());
        qReg->GetQuantumState(&(vec[0]));

        return make_tuple(map_, std::move(vec));
    }

    void run(){
        if (qReg != NULL) {
            qReg->Finish();
        }
    }

    ~QrackSimulator(){
    }

private:
    std::size_t get_control_mask(std::vector<unsigned> const& ctrls){
        std::size_t ctrlmask = 0;
        for (auto c : ctrls)
            ctrlmask |= (1UL << map_[c]);
        return ctrlmask;
    }

    bool check_ids(std::vector<unsigned> const& ids){
        for (auto id : ids)
            if (!map_.count(id))
                return false;
        return true;
    }

    void apply_uniformly_controlled(std::vector<calc_type> angles, std::vector<unsigned> ids, std::vector<unsigned> ctrl, UCRFunc fn){
        bitCapInt i;

        if (ctrl.size() > 0) {
            bitLenInt* ctrlArray = new bitLenInt[ctrl.size()];
            for (i = 0; i < ctrl.size(); i++) {
                ctrlArray[i] = map_[ctrl[i]];
            }

            for (i = 0; i < ids.size(); i++) {
                fn(ctrlArray, ctrl.size(), map_[ids[i]], &(angles[0]));
            }

            delete[] ctrlArray;
        } else {
            for (i = 0; i < ids.size(); i++) {
                fn(NULL, 0, map_[ids[i]], &(angles[0]));
            }
        }
    }

    void apply_controlled_int(CINTFunc fn, std::vector<unsigned> ids, std::vector<unsigned> ctrl){
        bitLenInt i;
        Map invMap;
        for (Map::iterator it = map_.begin(); it != map_.end(); it++) {
            invMap[it->second] = it->first;
        }

        bitLenInt tempMap;
        for (i = 0; i < ids.size(); i++) {
            qReg->Swap(i, map_[ids[i]]);

            tempMap = map_[ids[i]];
            std::swap(map_[ids[i]], map_[invMap[i]]);
            std::swap(invMap[i], invMap[tempMap]);
        }

        if (ctrl.size() > 0) {
            bitLenInt* ctrlArray = new bitLenInt[ctrl.size()];
            for (i = 0; i < ctrl.size(); i++) {
                ctrlArray[i] = map_[ctrl[i]];
            }

            fn(map_[ids[0]], (bitLenInt)ids.size(), ctrlArray, (bitLenInt)ctrl.size());

            delete[] ctrlArray;
        } else {
            fn(map_[ids[0]], (bitLenInt)ids.size(), NULL, 0);
        }
    }

    void apply_controlled_mulx(CMULXFunc fn, std::vector<unsigned> ids, std::vector<unsigned> ctrl){
        assert((ids.size() % 2) == 0);

        bitLenInt i;
        Map invMap;
        for (Map::iterator it = map_.begin(); it != map_.end(); it++) {
            invMap[it->second] = it->first;
        }

        bitLenInt tempMap;
        for (i = 0; i < ids.size(); i++) {
            qReg->Swap(i, map_[ids[i]]);

            tempMap = map_[ids[i]];
            std::swap(map_[ids[i]], map_[invMap[i]]);
            std::swap(invMap[i], invMap[tempMap]);
        }

        if (ctrl.size() > 0) {
            bitLenInt* ctrlArray = new bitLenInt[ctrl.size()];
            for (i = 0; i < ctrl.size(); i++) {
                ctrlArray[i] = map_[ctrl[i]];
            }

            fn(map_[ids[0]], (bitLenInt)(ids.size() / 2), (bitLenInt)(ids.size() / 2), ctrlArray, (bitLenInt)ctrl.size());

            delete[] ctrlArray;
        } else {
            fn(map_[ids[0]], (bitLenInt)(ids.size() / 2), (bitLenInt)(ids.size() / 2), NULL, 0);
        }
    }

    void apply_term(Term const& term, std::complex<calc_type> coeff, std::vector<unsigned> const& ids,
                    std::vector<unsigned> const& ctrl){

        std::complex<double> I(0., 1.);
        Matrix X = {{0., 1.}, {1., 0.}};
        Matrix Y = {{0., -I}, {I, 0.}};
        Matrix Z = {{1., 0.}, {0., -1.}};
        std::vector<Matrix> gates = {X, Y, Z};

        for (auto const& local_op : term){
            unsigned id = ids[local_op.first];
            auto temp = gates[local_op.second - 'X'];
            for (unsigned i = 0; i < 4; i++) {
                temp[i / 2][i % 2] *= -coeff;
            }
            apply_controlled_gate(temp, {id}, ctrl);
        }
    }

    calc_type diagonalize(Term const& term, std::complex<calc_type> coeff, std::vector<unsigned> const& ids){
        calc_type expectation = 1;
        calc_type angle = arg(coeff);
        calc_type len = abs(coeff);
        std::complex<double> phaseFac(cos(angle), sin(angle));
        bitCapInt idPower;

        std::complex<double> I(0., 1.);
        Matrix X = {{M_SQRT1_2, M_SQRT1_2}, {M_SQRT1_2, -M_SQRT1_2}};
        Matrix Y = {{M_SQRT1_2, -M_SQRT1_2 * I}, {M_SQRT1_2, M_SQRT1_2 * I}};
        Matrix Z = {{1., 0.}, {0., 1.}};
        std::vector<Matrix> gates = {X, Y, Z};

        for (auto const& local_op : term){
            unsigned id = ids[local_op.first];
            auto temp = gates[local_op.second - 'X'];
            for (unsigned i = 0; i < 4; i++) {
                temp[i / 2][i % 2] *= phaseFac;
            }
            apply_controlled_gate(temp, {id}, {});
            idPower = 1U << map_[id];
            expectation *= (qReg->ProbMask(idPower, 0) - qReg->ProbMask(idPower, idPower));
        }
        if (expectation > 1) {
            expectation = 1;
        }
        if (expectation < -1) {
            expectation = -1;
        }
        return len * expectation;
    }

    Map map_;
    std::shared_ptr<RndEngine> rnd_eng_;
    Qrack::QInterfacePtr qReg;
};

#endif
