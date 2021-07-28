#include "heyp/io/subprocess.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "boost/process/args.hpp"
#include "boost/process/async.hpp"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "heyp/log/logging.h"

namespace bp = boost::process;

namespace heyp {

absl::Status SubprocessResult::ErrorWhenRunning(absl::string_view name) {
  return absl::Status(status.code(),
                      absl::StrCat("failed to run ", name, ": ", status.message(),
                                   "; error output:\n", std::string(err)));
}

SubprocessResult RunSubprocess(const std::string& command,
                               const std::vector<std::string>& args, absl::Cord input) {
  VLOG(2) << "running " << command << " " << absl::StrJoin(args, " ");

  SubprocessResult result;

  try {
    boost::asio::io_context io;
    bp::async_pipe in_pipe(io);
    bp::async_pipe out_pipe(io);
    bp::async_pipe err_pipe(io);

    bp::child c(bp::search_path(command), bp::args(args), bp::std_out > out_pipe,
                bp::std_err > err_pipe, bp::std_in < in_pipe, io,
                bp::on_exit([&](auto...) {
                  out_pipe.close();
                  err_pipe.close();
                }));

    constexpr size_t kChunkSize = 4096;

    absl::Cord::ChunkIterator next_write_chunk = input.chunk_begin();
    size_t transferred = 0;
    std::function<void()> write_in_loop = [&] {
      if (next_write_chunk == input.chunk_end()) {
        in_pipe.close();
        return;
      }
      if (next_write_chunk->size() == transferred) {
        next_write_chunk = ++next_write_chunk;
        transferred = 0;
      }
      if (next_write_chunk == input.chunk_end()) {
        in_pipe.close();
        return;
      }

      boost::asio::async_write(in_pipe,
                               bp::buffer(next_write_chunk->data() + transferred,
                                          next_write_chunk->size() - transferred),
                               [&](boost::system::error_code ec, size_t diff) {
                                 transferred += diff;
                                 if (!ec) {
                                   write_in_loop();  // continue writing
                                 } else {
                                   result.status.Update(absl::UnknownError(absl::StrCat(
                                       "error writing stdin: ", ec.message())));
                                 }
                               });
    };

    std::function<void()> read_out_loop = [&,
                                           chunk_buf =
                                               std::array<char, kChunkSize>{}]() mutable {
      out_pipe.async_read_some(
          bp::buffer(chunk_buf), [&](boost::system::error_code ec, size_t transferred) {
            if (transferred) {
              result.out.Append(absl::string_view(chunk_buf.data(), transferred));
            }

            if (ec) {
              if (ec != boost::asio::error::eof) {
                result.status.Update(absl::UnknownError(
                    absl::StrCat("i/o error reading process stdout: ", ec.message())));
              }
            } else {
              read_out_loop();
            }
          });
    };

    std::function<void()> read_err_loop = [&,
                                           chunk_buf =
                                               std::array<char, kChunkSize>{}]() mutable {
      err_pipe.async_read_some(
          bp::buffer(chunk_buf), [&](boost::system::error_code ec, size_t transferred) {
            if (transferred) {
              result.err.Append(absl::string_view(chunk_buf.data(), transferred));
            }

            if (ec) {
              if (ec != boost::asio::error::eof) {
                result.status.Update(absl::UnknownError(
                    absl::StrCat("i/o error reading process stderr: ", ec.message())));
              }
            } else {
              read_err_loop();
            }
          });
    };

    write_in_loop();
    read_out_loop();
    read_err_loop();

    io.run();
    if (c.exit_code() != 0) {
      result.status.Update(
          absl::InternalError(absl::StrCat("exit status ", c.exit_code())));
    }
  } catch (const std::system_error& e) {
    result.status.Update(
        absl::InternalError(absl::StrCat("failed to run subprocess: ", e.what())));
  }
  return result;
}

}  // namespace heyp
