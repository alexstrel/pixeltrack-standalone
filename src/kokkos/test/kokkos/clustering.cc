#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <set>
#include <vector>

#include "KokkosCore/kokkosConfigCommon.h"
#include "KokkosCore/kokkosConfig.h"

// dirty, but works
#include "plugin-SiPixelClusterizer/kokkos/gpuClustering.h"
#include "plugin-SiPixelClusterizer/kokkos/gpuClusterChargeCut.h"

void test() {
  using namespace gpuClustering;

  int numElements = 256 * 2000;
  // these in reality are already on GPU
  Kokkos::View<uint16_t*, KokkosExecSpace>::HostMirror h_id("h_id", numElements);
  Kokkos::View<uint16_t*, KokkosExecSpace>::HostMirror h_x("h_x", numElements);
  Kokkos::View<uint16_t*, KokkosExecSpace>::HostMirror h_y("h_y", numElements);
  Kokkos::View<uint16_t*, KokkosExecSpace>::HostMirror h_adc("h_adc", numElements);

  Kokkos::View<int*, KokkosExecSpace>::HostMirror h_clus("h_clus", numElements);

  Kokkos::View<uint16_t*, KokkosExecSpace> d_id("d_id", numElements);
  Kokkos::View<uint16_t*, KokkosExecSpace> d_x("d_x", numElements);
  Kokkos::View<uint16_t*, KokkosExecSpace> d_y("d_y", numElements);
  Kokkos::View<uint16_t*, KokkosExecSpace> d_adc("d_adc", numElements);
  Kokkos::View<int*, KokkosExecSpace> d_clus("d_clus", numElements);
  Kokkos::View<uint32_t*, KokkosExecSpace> d_moduleStart("d_moduleStart", MaxNumModules + 1);
  Kokkos::View<uint32_t*, KokkosExecSpace> d_clusInModule("d_clusInModule", MaxNumModules);
  Kokkos::View<uint32_t*, KokkosExecSpace> d_moduleId("d_moduleId", MaxNumModules);

  Kokkos::View<uint32_t*, KokkosExecSpace>::HostMirror h_moduleStart("h_moduleStart", MaxNumModules + 1);
  Kokkos::View<uint32_t*, KokkosExecSpace>::HostMirror h_clusInModule("h_clusInModule", MaxNumModules);
  Kokkos::View<uint32_t*, KokkosExecSpace>::HostMirror h_moduleId("h_moduleId", MaxNumModules);

  // later random number
  int n = 0;
  int ncl = 0;
  int y[10] = {5, 7, 9, 1, 3, 0, 4, 8, 2, 6};

  auto generateClusters = [&](int kn) {
    auto addBigNoise = 1 == kn % 2;
    if (addBigNoise) {
      constexpr int MaxPixels = 1000;
      int id = 666;
      for (int x = 0; x < 140; x += 3) {
        for (int yy = 0; yy < 400; yy += 3) {
          h_id[n] = id;
          h_x[n] = x;
          h_y[n] = yy;
          h_adc[n] = 1000;
          ++n;
          ++ncl;
          if (MaxPixels <= ncl)
            break;
        }
        if (MaxPixels <= ncl)
          break;
      }
    }

    {
      // isolated
      int id = 42;
      int x = 10;
      ++ncl;
      h_id[n] = id;
      h_x[n] = x;
      h_y[n] = x;
      h_adc[n] = kn == 0 ? 100 : 5000;
      ++n;

      // first column
      ++ncl;
      h_id[n] = id;
      h_x[n] = x;
      h_y[n] = 0;
      h_adc[n] = 5000;
      ++n;
      // first columns
      ++ncl;
      h_id[n] = id;
      h_x[n] = x + 80;
      h_y[n] = 2;
      h_adc[n] = 5000;
      ++n;
      h_id[n] = id;
      h_x[n] = x + 80;
      h_y[n] = 1;
      h_adc[n] = 5000;
      ++n;

      // last column
      ++ncl;
      h_id[n] = id;
      h_x[n] = x;
      h_y[n] = 415;
      h_adc[n] = 5000;
      ++n;
      // last columns
      ++ncl;
      h_id[n] = id;
      h_x[n] = x + 80;
      h_y[n] = 415;
      h_adc[n] = 2500;
      ++n;
      h_id[n] = id;
      h_x[n] = x + 80;
      h_y[n] = 414;
      h_adc[n] = 2500;
      ++n;

      // diagonal
      ++ncl;
      for (int x = 20; x < 25; ++x) {
        h_id[n] = id;
        h_x[n] = x;
        h_y[n] = x;
        h_adc[n] = 1000;
        ++n;
      }
      ++ncl;
      // reversed
      for (int x = 45; x > 40; --x) {
        h_id[n] = id;
        h_x[n] = x;
        h_y[n] = x;
        h_adc[n] = 1000;
        ++n;
      }
      ++ncl;
      h_id[n++] = InvId;  // error
      // messy
      int xx[5] = {21, 25, 23, 24, 22};
      for (int k = 0; k < 5; ++k) {
        h_id[n] = id;
        h_x[n] = xx[k];
        h_y[n] = 20 + xx[k];
        h_adc[n] = 1000;
        ++n;
      }
      // holes
      ++ncl;
      for (int k = 0; k < 5; ++k) {
        h_id[n] = id;
        h_x[n] = xx[k];
        h_y[n] = 100;
        h_adc[n] = kn == 2 ? 100 : 1000;
        ++n;
        if (xx[k] % 2 == 0) {
          h_id[n] = id;
          h_x[n] = xx[k];
          h_y[n] = 101;
          h_adc[n] = 1000;
          ++n;
        }
      }
    }
    {
      // id == 0 (make sure it works!
      int id = 0;
      int x = 10;
      ++ncl;
      h_id[n] = id;
      h_x[n] = x;
      h_y[n] = x;
      h_adc[n] = 5000;
      ++n;
    }
    // all odd id
    for (int id = 11; id <= 1800; id += 2) {
      if ((id / 20) % 2)
        h_id[n++] = InvId;  // error
      for (int x = 0; x < 40; x += 4) {
        ++ncl;
        if ((id / 10) % 2) {
          for (int k = 0; k < 10; ++k) {
            h_id[n] = id;
            h_x[n] = x;
            h_y[n] = x + y[k];
            h_adc[n] = 100;
            ++n;
            h_id[n] = id;
            h_x[n] = x + 1;
            h_y[n] = x + y[k] + 2;
            h_adc[n] = 1000;
            ++n;
          }
        } else {
          for (int k = 0; k < 10; ++k) {
            h_id[n] = id;
            h_x[n] = x;
            h_y[n] = x + y[9 - k];
            h_adc[n] = kn == 2 ? 10 : 1000;
            ++n;
            if (y[k] == 3)
              continue;  // hole
            if (id == 51) {
              h_id[n++] = InvId;
              h_id[n++] = InvId;
            }  // error
            h_id[n] = id;
            h_x[n] = x + 1;
            h_y[n] = x + y[k] + 2;
            h_adc[n] = kn == 2 ? 10 : 1000;
            ++n;
          }
        }
      }
    }
  };  // end lambda
  for (auto kkk = 0; kkk < 5; ++kkk) {
    n = 0;
    ncl = 0;
    generateClusters(kkk);

    std::cout << "created " << n << " digis in " << ncl << " clusters" << std::endl;
    assert(n <= numElements);

    uint32_t nModules = 0;
    size_t size32 = n * sizeof(unsigned int);
    size_t size16 = n * sizeof(unsigned short);
    // size_t size8 = n * sizeof(uint8_t);

    Kokkos::deep_copy(KokkosExecSpace(), d_moduleStart, 0);
    Kokkos::deep_copy(KokkosExecSpace(), d_id, h_id);
    Kokkos::deep_copy(KokkosExecSpace(), d_x, h_x);
    Kokkos::deep_copy(KokkosExecSpace(), d_y, h_y);
    Kokkos::deep_copy(KokkosExecSpace(), d_adc, h_adc);

    // Launch Kokkos Kernels
    std::cout << "Kokkos countModules kernel launch\n";
    Kokkos::parallel_for(
        Kokkos::RangePolicy<KokkosExecSpace>(0, n), KOKKOS_LAMBDA(const size_t i) {
          KOKKOS_NAMESPACE::gpuClustering::countModules(d_id, d_moduleStart, d_clus, n, i);
        });
    KokkosExecSpace().fence();
#ifdef TODO
#ifdef __CUDACC__
    int threadsPerBlock = (kkk == 5) ? 512 : ((kkk == 3) ? 128 : 256);
    int blocksPerGrid = (numElements + threadsPerBlock - 1) / threadsPerBlock;

    blocksPerGrid = MaxNumModules;  //nModules;

    std::cout << "CUDA findModules kernel launch with " << blocksPerGrid << " blocks of " << threadsPerBlock
              << " threads\n";
    cudaCheck(cudaMemset(d_clusInModule.get(), 0, MaxNumModules * sizeof(uint32_t)));

    cms::cuda::launch(findClus,
                      {blocksPerGrid, threadsPerBlock},
                      d_id.get(),
                      d_x.get(),
                      d_y.get(),
                      d_moduleStart.get(),
                      d_clusInModule.get(),
                      d_moduleId.get(),
                      d_clus.get(),
                      n);
    cudaDeviceSynchronize();
    cudaCheck(cudaMemcpy(&nModules, d_moduleStart.get(), sizeof(uint32_t), cudaMemcpyDeviceToHost));

    uint32_t nclus[MaxNumModules], moduleId[nModules];
    cudaCheck(cudaMemcpy(&nclus, d_clusInModule.get(), MaxNumModules * sizeof(uint32_t), cudaMemcpyDeviceToHost));

    std::cout << "before charge cut found " << std::accumulate(nclus, nclus + MaxNumModules, 0) << " clusters"
              << std::endl;
    for (auto i = MaxNumModules; i > 0; i--)
      if (nclus[i - 1] > 0) {
        std::cout << "last module is " << i - 1 << ' ' << nclus[i - 1] << std::endl;
        break;
      }
    if (ncl != std::accumulate(nclus, nclus + MaxNumModules, 0))
      std::cout << "ERROR!!!!! wrong number of cluster found" << std::endl;

    cms::cuda::launch(clusterChargeCut,
                      {blocksPerGrid, threadsPerBlock},
                      d_id.get(),
                      d_adc.get(),
                      d_moduleStart.get(),
                      d_clusInModule.get(),
                      d_moduleId.get(),
                      d_clus.get(),
                      n);

    cudaDeviceSynchronize();
#else
    h_moduleStart[0] = nModules;
    countModules(h_id.get(), h_moduleStart.get(), h_clus.get(), n);
    memset(h_clusInModule.get(), 0, MaxNumModules * sizeof(uint32_t));
    gridDim.x = MaxNumModules;  //not needed in the kernel for this specific case;
    assert(blockIdx.x == 0);
    for (; blockIdx.x < gridDim.x; ++blockIdx.x)
      findClus(h_id.get(),
               h_x.get(),
               h_y.get(),
               h_moduleStart.get(),
               h_clusInModule.get(),
               h_moduleId.get(),
               h_clus.get(),
               n);
    resetGrid();

    nModules = h_moduleStart[0];
    auto nclus = h_clusInModule.get();

    std::cout << "before charge cut found " << std::accumulate(nclus, nclus + MaxNumModules, 0) << " clusters"
              << std::endl;
    for (auto i = MaxNumModules; i > 0; i--)
      if (nclus[i - 1] > 0) {
        std::cout << "last module is " << i - 1 << ' ' << nclus[i - 1] << std::endl;
        break;
      }
    if (ncl != std::accumulate(nclus, nclus + MaxNumModules, 0))
      std::cout << "ERROR!!!!! wrong number of cluster found" << std::endl;

    gridDim.x = MaxNumModules;  // no needed in the kernel for in this specific case
    assert(blockIdx.x == 0);
    for (; blockIdx.x < gridDim.x; ++blockIdx.x)
      clusterChargeCut(
          h_id.get(), h_adc.get(), h_moduleStart.get(), h_clusInModule.get(), h_moduleId.get(), h_clus.get(), n);
    resetGrid();

#endif

    std::cout << "found " << nModules << " Modules active" << std::endl;

#ifdef __CUDACC__
    cudaCheck(cudaMemcpy(h_id.get(), d_id.get(), size16, cudaMemcpyDeviceToHost));
    cudaCheck(cudaMemcpy(h_clus.get(), d_clus.get(), size32, cudaMemcpyDeviceToHost));
    cudaCheck(cudaMemcpy(&nclus, d_clusInModule.get(), MaxNumModules * sizeof(uint32_t), cudaMemcpyDeviceToHost));
    cudaCheck(cudaMemcpy(&moduleId, d_moduleId.get(), nModules * sizeof(uint32_t), cudaMemcpyDeviceToHost));
#endif

    std::set<unsigned int> clids;
    for (int i = 0; i < n; ++i) {
      assert(h_id[i] != 666);  // only noise
      if (h_id[i] == InvId)
        continue;
      assert(h_clus[i] >= 0);
      assert(h_clus[i] < int(nclus[h_id[i]]));
      clids.insert(h_id[i] * 1000 + h_clus[i]);
      // clids.insert(h_clus[i]);
    }

    // verify no hole in numbering
    auto p = clids.begin();
    auto cmid = (*p) / 1000;
    assert(0 == (*p) % 1000);
    auto c = p;
    ++c;
    std::cout << "first clusters " << *p << ' ' << *c << ' ' << nclus[cmid] << ' ' << nclus[(*c) / 1000] << std::endl;
    std::cout << "last cluster " << *clids.rbegin() << ' ' << nclus[(*clids.rbegin()) / 1000] << std::endl;
    for (; c != clids.end(); ++c) {
      auto cc = *c;
      auto pp = *p;
      auto mid = cc / 1000;
      auto pnc = pp % 1000;
      auto nc = cc % 1000;
      if (mid != cmid) {
        assert(0 == cc % 1000);
        assert(nclus[cmid] - 1 == pp % 1000);
        // if (nclus[cmid]-1 != pp%1000) std::cout << "error size " << mid << ": "  << nclus[mid] << ' ' << pp << std::endl;
        cmid = mid;
        p = c;
        continue;
      }
      p = c;
      // assert(nc==pnc+1);
      if (nc != pnc + 1)
        std::cout << "error " << mid << ": " << nc << ' ' << pnc << std::endl;
    }

    std::cout << "found " << std::accumulate(nclus, nclus + MaxNumModules, 0) << ' ' << clids.size() << " clusters"
              << std::endl;
    for (auto i = MaxNumModules; i > 0; i--)
      if (nclus[i - 1] > 0) {
        std::cout << "last module is " << i - 1 << ' ' << nclus[i - 1] << std::endl;
        break;
      }
      // << " and " << seeds.size() << " seeds" << std::endl;
#endif  // TODO
  }     /// end loop kkk
}

int main(void) {
  kokkos_common::InitializeScopeGuard kokkosGuard({KokkosBackend<KokkosExecSpace>::value});
  test();
  return 0;
}
