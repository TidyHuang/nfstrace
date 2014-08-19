//------------------------------------------------------------------------------
// Author: Dzianis Huznou (Alexey Costroma)
// Description: Created for demonstration purpose only.
// Copyright (c) 2013,2014 EPAM Systems
//------------------------------------------------------------------------------
/*
    This file is part of Nfstrace.

    Nfstrace is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2 of the License.

    Nfstrace is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Nfstrace.  If not, see <http://www.gnu.org/licenses/>.
*/
//------------------------------------------------------------------------------
#include "analysis/print_analyzer.h"
#include "protocols/nfs3/nfs3_utils.h"
#include "protocols/nfs4/nfs4_utils.h"
#include "protocols/rpc/rpc_utils.h"
#include "utils/out.h"
#include "utils/sessions.h"
//------------------------------------------------------------------------------
namespace NST
{
namespace analysis
{

using namespace NST::protocols::NFS3;   // NFSv3 helpers
using namespace NST::protocols::NFS4;   // NFSv4 helpers
using namespace NST::protocols::rpc;    // Sun/RPC helpers

namespace
{

inline bool out_all()
{
    using Out = NST::utils::Out;

    return Out::Global::get_level() == Out::Level::All;
}

// Special helper for print-out short representation of NFS FH
std::ostream& print_nfs_fh3(std::ostream& out, const uint8_t* data, const uint32_t size)
{
    out << std::hex << std::setfill('0') << std::setw(2);
    if(size <= 8 || out_all())
    {
        for(uint32_t j = 0; j < size; j++)
        {
            out << std::setw(2) << (uint32_t)data[j];
        }
    }
    else // truncate binary data to: 00112233...CCDDEEFF
    {
        for(uint32_t j = 0; j < 4; j++)
        {
            out << std::setw(2) << (uint32_t)data[j];
        }
        out << "...";
        for(uint32_t j = size-4; j < size; j++)
        {
            out << std::setw(2) << (uint32_t)data[j];
        }
    }
    out << std::dec << std::setfill(' ');
    return out;
}

std::ostream& print_nfs_fh3(std::ostream& out, const nfs_fh3& fh)
{
    return print_nfs_fh3(out, fh.data.data(), fh.data.size());
}

extern "C"
void print_nfs_fh3(std::ostream& out, const FH& fh)
{
    print_nfs_fh3(out, fh.data, fh.len);
}

bool print_procedure(std::ostream& out, const struct RPCProcedure* proc)
{
    bool result = false;
    NST::utils::operator<<(out, *(proc->session));

    auto& call = proc->rpc_call;
    const int nfs_version = call.ru.RM_cmb.cb_vers;
    if(out_all())
    {
        out << " XID: "         << call.rm_xid;
        out << " RPC version: " << call.ru.RM_cmb.cb_rpcvers;
        out << " RPC program: " << call.ru.RM_cmb.cb_prog;
        out << " version: "     << nfs_version;
    }
    switch(nfs_version)
    {
    case NFS_V3:
        out << ' ' << print_nfs3_procedures(static_cast<ProcEnumNFS3::NFSProcedure>(call.ru.RM_cmb.cb_proc));
        break;
    case NFS_V4:
        out << ' ' << print_nfs4_procedures(static_cast<ProcEnumNFS4::NFSProcedure>(call.ru.RM_cmb.cb_proc));
        break;
    }

    // check procedure reply
    auto& reply = proc->rpc_reply;
    if(reply.ru.RM_rmb.rp_stat == reply_stat::MSG_ACCEPTED)
    {
        switch(reply.ru.RM_rmb.ru.RP_ar.ar_stat)
        {
            case accept_stat::SUCCESS:
                result = true;    // Ok, reply is correct
                break;
            case accept_stat::PROG_MISMATCH:
                out << " Program mismatch: "
                    << " low: "  << reply.ru.RM_rmb.ru.RP_ar.ru.AR_versions.low
                    << " high: " << reply.ru.RM_rmb.ru.RP_ar.ru.AR_versions.high;
                break;
            case accept_stat::PROG_UNAVAIL:
                out << " Program unavailable";
                break;
            case accept_stat::PROC_UNAVAIL:
                out << " Procedure unavailable";
                break;
            case accept_stat::GARBAGE_ARGS:
                out << " Garbage arguments";
                break;
            case accept_stat::SYSTEM_ERR:
                out << " System error";
                break;
        }
    }
    else if(reply.ru.RM_rmb.rp_stat == reply_stat::MSG_DENIED)
    {
        out << " RPC Call rejected: ";
        switch(reply.ru.RM_rmb.ru.RP_dr.rj_stat)
        {
            case reject_stat::RPC_MISMATCH:
                out << "RPC version number mismatch, "
                    << " low: "  << reply.ru.RM_rmb.ru.RP_dr.ru.RJ_versions.low
                    << " high: " << reply.ru.RM_rmb.ru.RP_dr.ru.RJ_versions.high;
                break;
            case reject_stat::AUTH_ERROR:
            {
                out << " Authentication check: ";
                switch(reply.ru.RM_rmb.ru.RP_dr.ru.RJ_why)
                {
                case auth_stat::AUTH_OK:
                    out << "OK";
                    break;
                case auth_stat::AUTH_BADCRED:
                    out << " bogus credentials (seal broken)"
                        << " (failed at remote end)";
                    break;
                case auth_stat::AUTH_REJECTEDCRED:
                    out << " rejected credentials (client should begin new session)"
                        << " (failed at remote end)";
                    break;
                case auth_stat::AUTH_BADVERF:
                    out << " bogus verifier (seal broken)"
                        << " (failed at remote end)";
                    break;
                case auth_stat::AUTH_REJECTEDVERF:
                    out << " verifier expired or was replayed"
                        << " (failed at remote end)";
                    break;
                case auth_stat::AUTH_TOOWEAK:
                    out << " too weak (rejected due to security reasons)"
                        << " (failed at remote end)";
                    break;
                case auth_stat::AUTH_INVALIDRESP:
                    out << " bogus response verifier"
                        << " (failed locally)";
                    break;
                case auth_stat::AUTH_FAILED:
                    out << " some unknown reason"
                        << " (failed locally)";
                    break;
                }
                break;
            }
        }
    }

    out << '\n'; // end line of RPC procedure information
    return result;
}

} // unnamed namespace

// Print NFSv3 procedures
// 1st line - PRC information: src and dst hosts, status of RPC procedure
// 2nd line - <tabulation>related RPC procedure-specific arguments
// 3rd line - <tabulation>related RPC procedure-specific results

void PrintAnalyzer::null(const struct RPCProcedure* proc,
                         const struct NULLargs*,
                         const struct NULLres*)
{
    if(!print_procedure(out, proc)) return;

    out << "\tCALL  []\n\tREPLY []\n";
}

void PrintAnalyzer::getattr3(const RPCProcedure*        proc,
                             const struct GETATTR3args* args,
                             const struct GETATTR3res*  res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " object: "; print_nfs_fh3(out, args->object);
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(res->status == nfsstat3::OK && out_all())
        {
            out << " obj_attributes: " << res->resok.obj_attributes;
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::setattr3(const RPCProcedure*        proc,
                             const struct SETATTR3args* args,
                             const struct SETATTR3res*  res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " object: "; print_nfs_fh3(out, args->object);
        out << " new_attributes: " << args->new_attributes;
        out << " guard: "          << args->guard;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj_wcc: " << res->resok.obj_wcc;
            }
            else
            {
                out << " obj_wcc: " << res->resfail.obj_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::lookup3(const RPCProcedure*       proc,
                            const struct LOOKUP3args* args,
                            const struct LOOKUP3res*  res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " what: " << args->what;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " object: "; print_nfs_fh3(out, res->resok.object);
                out << " obj_attributes: "  << res->resok.obj_attributes;
                out << " dir_attributes: "  << res->resok.dir_attributes;
            }
            else
            {
                out << " dir_attributes: "  << res->resfail.dir_attributes;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::access3(const struct RPCProcedure* proc,
                            const struct ACCESS3args*  args,
                            const struct ACCESS3res*   res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " object: "; print_nfs_fh3(out, args->object);
        out << " access: ";
        if(args->access & ACCESS3args::ACCESS3_READ)   out << "READ ";
        if(args->access & ACCESS3args::ACCESS3_LOOKUP) out << "LOOKUP ";
        if(args->access & ACCESS3args::ACCESS3_MODIFY) out << "MODIFY ";
        if(args->access & ACCESS3args::ACCESS3_EXTEND) out << "EXTEND ";
        if(args->access & ACCESS3args::ACCESS3_DELETE) out << "DELETE ";
        if(args->access & ACCESS3args::ACCESS3_EXECUTE)out << "EXECUTE ";
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj_attributes: " << res->resok.obj_attributes;
                out << " access: ";
                uint32_t access = res->resok.access;
                if(access & ACCESS3args::ACCESS3_READ)   out << "READ ";
                if(access & ACCESS3args::ACCESS3_LOOKUP) out << "LOOKUP ";
                if(access & ACCESS3args::ACCESS3_MODIFY) out << "MODIFY ";
                if(access & ACCESS3args::ACCESS3_EXTEND) out << "EXTEND ";
                if(access & ACCESS3args::ACCESS3_DELETE) out << "DELETE ";
                if(access & ACCESS3args::ACCESS3_EXECUTE)out << "EXECUTE ";
            }
            else
            {
                out << " obj_attributes: " << res->resfail.obj_attributes;
                out << ' ';
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::readlink3(const struct RPCProcedure*  proc,
                              const struct READLINK3args* args,
                              const struct READLINK3res*  res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " symlink: "; print_nfs_fh3(out, args->symlink);
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " symlink_attributes: " << res->resok.symlink_attributes;
                out << " data: "  << to_string(res->resok.data);
            }
            else
            {
                out << " symlink_attributes: " << res->resfail.symlink_attributes;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::read3(const struct RPCProcedure* proc,
                          const struct READ3args*    args,
                          const struct READ3res*     res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " file: "; print_nfs_fh3(out, args->file);
        out << " offset: " << args->offset;
        out << " count: "  << args->count;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " file_attributes: " << res->resok.file_attributes;
                out << " count: "           << res->resok.count;
                out << " eof: "             << bool(res->resok.eof);
                out << " data: skipped on filtration";
            }
            else
            {
                out << " file_attributes: " << res->resfail.file_attributes;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::write3(const struct RPCProcedure* proc,
                           const struct WRITE3args*   args,
                           const struct WRITE3res*    res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " file: "; print_nfs_fh3(out, args->file);
        out << " offset: " << args->offset;
        out << " count: "  << args->count;
        out << " stable: " << args->stable;
        if(out_all())
        {
            out << " data: skipped on filtration";
        }
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " file_wcc: "  << res->resok.file_wcc;
                out << " count: "     << res->resok.count;
                out << " committed: " << res->resok.committed;
                out << " verf: "      << res->resok.verf;
            }
            else
            {
                out << " file_wcc: " << res->resfail.file_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::create3(const struct RPCProcedure* proc,
                            const struct CREATE3args*  args,
                            const struct CREATE3res*   res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " where: " << args->where;
        out << " how: "   << args->how;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj: "            << res->resok.obj;
                out << " obj_attributes: " << res->resok.obj_attributes;
                out << " dir_wcc: "        << res->resok.dir_wcc;
            }
            else
            {
                out << " dir_wcc: " << res->resfail.dir_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::mkdir3(const struct RPCProcedure* proc,
                           const struct MKDIR3args*   args,
                           const struct MKDIR3res*    res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " where: "      << args->where;
        out << " attributes: " << args->attributes;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj: "            << res->resok.obj;
                out << " obj_attributes: " << res->resok.obj_attributes;
                out << " dir_wcc: "        << res->resok.dir_wcc;
            }
            else
            {
                out << " dir_wcc: " << res->resfail.dir_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::symlink3(const struct RPCProcedure* proc,
                             const struct SYMLINK3args* args,
                             const struct SYMLINK3res*  res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " where: "       << args->where;
        out << " symlinkdata: " << args->symlink;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj: "            << res->resok.obj;
                out << " obj_attributes: " << res->resok.obj_attributes;
                out << " dir_wcc: "        << res->resok.dir_wcc;
            }
            else
            {
                out << " dir_wcc: " << res->resfail.dir_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::mknod3(const struct RPCProcedure* proc,
                           const struct MKNOD3args*   args,
                           const struct MKNOD3res*    res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " where: " << args->where;
        out << " what: "  << args->what;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj: "            << res->resok.obj;
                out << " obj_attributes: " << res->resok.obj_attributes;
                out << " dir_wcc: "        << res->resok.dir_wcc;
            }
            else
            {
                out << " dir_wcc: " << res->resfail.dir_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::remove3(const struct RPCProcedure* proc,
                            const struct REMOVE3args*  args,
                            const struct REMOVE3res*   res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " object: " << args->object;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " dir_wcc: " << res->resok.dir_wcc;
            }
            else
            {
                out << " dir_wcc: " << res->resfail.dir_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::rmdir3(const struct RPCProcedure* proc,
                           const struct RMDIR3args*   args,
                           const struct RMDIR3res*    res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " object: " << args->object;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " dir_wcc: " << res->resok.dir_wcc;
            }
            else
            {
                out << " dir_wcc: " << res->resfail.dir_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::rename3(const struct RPCProcedure* proc,
                            const struct RENAME3args*  args,
                            const struct RENAME3res*   res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " from: " << args->from;
        out << " to: "   << args->to;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " fromdir_wcc: " << res->resok.fromdir_wcc;
                out << " todir_wcc: "   << res->resok.todir_wcc;
            }
            else
            {
                out << " fromdir_wcc: " << res->resfail.fromdir_wcc;
                out << " todir_wcc: "   << res->resfail.todir_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::link3(const struct RPCProcedure* proc,
                          const struct LINK3args*    args,
                          const struct LINK3res*     res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " file: "; print_nfs_fh3(out, args->file);;
        out << " link: " << args->link;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " file_attributes: " << res->resok.file_attributes;
                out << " linkdir_wcc: "     << res->resok.linkdir_wcc;
            }
            else
            {
                out << " file_attributes: " << res->resfail.file_attributes;
                out << " linkdir_wcc: "     << res->resfail.linkdir_wcc;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::readdir3(const struct RPCProcedure* proc,
                             const struct READDIR3args* args,
                             const struct READDIR3res* res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " dir: "; print_nfs_fh3(out, args->dir);
        out << " cookie: "      << args->cookie;
        out << " cookieverf: "  << args->cookieverf;
        out << " count: "       << args->count;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " dir_attributes: " << res->resok.dir_attributes;
                out << " cookieverf: "     << res->resok.cookieverf;
                out << " reply: Not implemented";//     << res->resok.reply;
            }
            else
            {
                out << " dir_attributes: " << res->resfail.dir_attributes;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::readdirplus3(const struct RPCProcedure*     proc,
                                 const struct READDIRPLUS3args* args,
                                 const struct READDIRPLUS3res*  res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " dir: "; print_nfs_fh3(out, args->dir);
        out << " cookie: "      << args->cookie;
        out << " cookieverf: "  << args->cookieverf;
        out << " dircount: "    << args->dircount;
        out << " maxcount: "    << args->maxcount;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " dir_attributes: " << res->resok.dir_attributes;
                out << " cookieverf: "     << res->resok.cookieverf;
                out << " reply: Not implemented";//     << res->resok.reply;
            }
            else
            {
                out << " dir_attributes: " << res->resfail.dir_attributes;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::fsstat3(const struct RPCProcedure* proc,
                            const struct FSSTAT3args*  args,
                            const struct FSSTAT3res*   res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " fsroot: "; print_nfs_fh3(out, args->fsroot);
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj_attributes: " << res->resok.obj_attributes;
                out << " tbytes: "     << res->resok.tbytes;
                out << " fbytes: "     << res->resok.fbytes;
                out << " abytes: "     << res->resok.abytes;
                out << " tfiles: "     << res->resok.tfiles;
                out << " ffiles: "     << res->resok.ffiles;
                out << " afiles: "     << res->resok.afiles;
                out << " invarsec: "   << res->resok.invarsec;
            }
            else
            {
                out << " obj_attributes: " << res->resfail.obj_attributes;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::fsinfo3(const struct RPCProcedure* proc,
                            const struct FSINFO3args*  args,
                            const struct FSINFO3res*   res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " fsroot: "; print_nfs_fh3(out, args->fsroot);
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj_attributes: " << res->resok.obj_attributes;
                out << " rtmax: "       << res->resok.rtmax;
                out << " rtpref: "      << res->resok.rtpref;
                out << " rtmult: "      << res->resok.rtmult;
                out << " wtmax: "       << res->resok.wtmax;
                out << " wtpref: "      << res->resok.wtpref;
                out << " wtmult: "      << res->resok.wtmult;
                out << " dtpref: "      << res->resok.dtpref;
                out << " maxfilesize: " << res->resok.maxfilesize;
                out << " time_delta: "  << res->resok.time_delta;
                out << " properties: ";
                out << " LINK:"        << bool(res->resok.properties & FSINFO3res::FSINFO3resok::FSF3_LINK);
                out << " SYMLINK:"     << bool(res->resok.properties & FSINFO3res::FSINFO3resok::FSF3_SYMLINK);
                out << " HOMOGENEOUS:" << bool(res->resok.properties & FSINFO3res::FSINFO3resok::FSF3_HOMOGENEOUS);
                out << " CANSETTIME:"  << bool(res->resok.properties & FSINFO3res::FSINFO3resok::FSF3_CANSETTIME);
            }
            else
            {
                out << " obj_attributes: " << res->resfail.obj_attributes;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::pathconf3(const struct RPCProcedure*  proc,
                              const struct PATHCONF3args* args,
                              const struct PATHCONF3res*  res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " object: "; print_nfs_fh3(out, args->object);
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " obj_attributes: "   << res->resok.obj_attributes;
                out << " linkmax: "          << res->resok.linkmax;
                out << " name_max: "         << res->resok.name_max;
                out << " no_trunc: "         << bool(res->resok.no_trunc);
                out << " chown_restricted: " << bool(res->resok.chown_restricted);
                out << " case_insensitive: " << bool(res->resok.case_insensitive);
                out << " case_preserving: "  << bool(res->resok.case_preserving);
            }
            else
            {
                out << " obj_attributes: " << res->resfail.obj_attributes;
            }
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::commit3(const struct RPCProcedure* proc,
                            const struct COMMIT3args*  args,
                            const struct COMMIT3res*   res)
{
    if(!print_procedure(out, proc)) return;

    if(args)
    {
        out << "\tCALL  [";
        out << " file: "; print_nfs_fh3(out, args->file);
        out << " offset: "  << args->offset;
        out << " count: "   << args->count;
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " status: " << res->status;
        if(out_all())
        {
            if(res->status == nfsstat3::OK)
            {
                out << " file_wcc: " << res->resok.file_wcc;
                out << " verf: "     << res->resok.verf;
            }
            else
            {
                out << " file_wcc: " << res->resfail.file_wcc;
            }
        }
        out << " ]\n";
    }
}

// Print NFSv4 procedures
// 1st line - PRC information: src and dst hosts, status of RPC procedure
// 2nd line - <tabulation>related RPC procedure-specific arguments
// 3rd line - <tabulation>related NFSv4-operations
// 4th line - <tabulation>related RPC procedure-specific results
// 5rd line - <tabulation>related NFSv4-operations

void PrintAnalyzer::null(const struct RPCProcedure* proc,
                         const struct rpcgen::NULL4args*,
                         const struct rpcgen::NULL4res*)
{
    if(!print_procedure(out, proc)) return;

    out << "\tCALL  []\n\tREPLY []\n";
}

void PrintAnalyzer::compound4(const struct RPCProcedure*          proc,
                              const struct rpcgen::COMPOUND4args* args,
                              const struct rpcgen::COMPOUND4res*  res)
{
    if(!print_procedure(out, proc)) return;
    if(args)
    {
        out << "\tCALL  [";
        out << " operations: " << args->argarray.argarray_len;
        out << " tag: " << args->tag;
        out << " minor version: " << args->minorversion;
        rpcgen::nfs_argop4* current_el = args->argarray.argarray_val;
        for(u_int i=0; i<(args->argarray.argarray_len); i++, current_el++)
        {
            out << "\n\t\t" << print_nfs4_procedures(static_cast<ProcEnumNFS4::NFSProcedure>(current_el->argop)) << "(" << current_el->argop << ") [ ";
            nfs4_operation(current_el);
            out << " ]";
        }
        out << " ]\n";
    }
    if(res)
    {
        out << "\tREPLY [";
        out << " operations: " << res->resarray.resarray_len;
        rpcgen::nfs_resop4* current_el = res->resarray.resarray_val;
        for(u_int i=0; i<(res->resarray.resarray_len); i++, current_el++)
        {
            u_int nfs_oper = current_el->resop;
            // In all cases we suppose, that NFSv4 operation ILLEGAL(10044)
            // has the second position in ProcEnumNFS4
            if(nfs_oper == ProcEnumNFS4::NFSProcedure::ILLEGAL) nfs_oper = 2;
            out << "\n\t\t" << print_nfs4_procedures(static_cast<ProcEnumNFS4::NFSProcedure>(nfs_oper)) << "(" << current_el->resop << ") [ ";
            nfs4_operation(current_el);
            out << " ]";
        }
        out << " ]\n";
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::nfs_argop4* op)
{
    if(op)
    {
    switch(op->argop)
    {
    case rpcgen::OP_ACCESS:              nfs4_operation(&op->nfs_argop4_u.opaccess);              break;
    case rpcgen::OP_CLOSE:               nfs4_operation(&op->nfs_argop4_u.opclose);               break;
    case rpcgen::OP_COMMIT:              nfs4_operation(&op->nfs_argop4_u.opcommit);              break;
    case rpcgen::OP_CREATE:              nfs4_operation(&op->nfs_argop4_u.opcreate);              break;
    case rpcgen::OP_DELEGPURGE:          nfs4_operation(&op->nfs_argop4_u.opdelegpurge);          break;
    case rpcgen::OP_DELEGRETURN:         nfs4_operation(&op->nfs_argop4_u.opdelegreturn);         break;
    case rpcgen::OP_GETATTR:             nfs4_operation(&op->nfs_argop4_u.opgetattr);             break;
    case rpcgen::OP_GETFH:               /* no such operation in call procedure */                break;
    case rpcgen::OP_LINK:                nfs4_operation(&op->nfs_argop4_u.oplink);                break;
    case rpcgen::OP_LOCK:                nfs4_operation(&op->nfs_argop4_u.oplock);                break;
    case rpcgen::OP_LOCKT:               nfs4_operation(&op->nfs_argop4_u.oplockt);               break;
    case rpcgen::OP_LOCKU:               nfs4_operation(&op->nfs_argop4_u.oplocku);               break;
    case rpcgen::OP_LOOKUP:              nfs4_operation(&op->nfs_argop4_u.oplookup);              break;
    case rpcgen::OP_LOOKUPP:             /* no such operation in call procedure */                break;
    case rpcgen::OP_NVERIFY:             nfs4_operation(&op->nfs_argop4_u.opnverify);             break;
    case rpcgen::OP_OPEN:                nfs4_operation(&op->nfs_argop4_u.opopen);                break;
    case rpcgen::OP_OPENATTR:            nfs4_operation(&op->nfs_argop4_u.opopenattr);            break;
    case rpcgen::OP_OPEN_CONFIRM:        nfs4_operation(&op->nfs_argop4_u.opopen_confirm);        break;
    case rpcgen::OP_OPEN_DOWNGRADE:      nfs4_operation(&op->nfs_argop4_u.opopen_downgrade);      break;
    case rpcgen::OP_PUTFH:               nfs4_operation(&op->nfs_argop4_u.opputfh);               break;
    case rpcgen::OP_PUTPUBFH:            /* no such operation in call procedure */                break;
    case rpcgen::OP_PUTROOTFH:           /* no such operation in call procedure */                break;
    case rpcgen::OP_READ:                nfs4_operation(&op->nfs_argop4_u.opread);                break;
    case rpcgen::OP_READDIR:             nfs4_operation(&op->nfs_argop4_u.opreaddir);             break;
    case rpcgen::OP_READLINK:            /* no such operation in call procedure */                break;
    case rpcgen::OP_REMOVE:              nfs4_operation(&op->nfs_argop4_u.opremove);              break;
    case rpcgen::OP_RENAME:              nfs4_operation(&op->nfs_argop4_u.oprename);              break;
    case rpcgen::OP_RENEW:               nfs4_operation(&op->nfs_argop4_u.oprenew);               break;
    case rpcgen::OP_RESTOREFH:           /* no such operation in call procedure */                break;
    case rpcgen::OP_SAVEFH:              /* no such operation in call procedure */                break;
    case rpcgen::OP_SECINFO:             nfs4_operation(&op->nfs_argop4_u.opsecinfo);             break;
    case rpcgen::OP_SETATTR:             nfs4_operation(&op->nfs_argop4_u.opsetattr);             break;
    case rpcgen::OP_SETCLIENTID:         nfs4_operation(&op->nfs_argop4_u.opsetclientid);         break;
    case rpcgen::OP_SETCLIENTID_CONFIRM: nfs4_operation(&op->nfs_argop4_u.opsetclientid_confirm); break;
    case rpcgen::OP_VERIFY:              nfs4_operation(&op->nfs_argop4_u.opverify);              break;
    case rpcgen::OP_WRITE:               nfs4_operation(&op->nfs_argop4_u.opwrite);               break;
    case rpcgen::OP_RELEASE_LOCKOWNER:   nfs4_operation(&op->nfs_argop4_u.oprelease_lockowner);   break;
    case rpcgen::OP_GET_DIR_DELEGATION:  nfs4_operation(&op->nfs_argop4_u.opget_dir_delegation);  break;
    case rpcgen::OP_ILLEGAL:             /* no such operation in call procedure */                break;
    }//switch
    }//if
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::nfs_resop4* op)
{
    if(op)
    {
    switch(op->resop)
    {
    case rpcgen::OP_ACCESS:              nfs4_operation(&op->nfs_resop4_u.opaccess);              break;
    case rpcgen::OP_CLOSE:               nfs4_operation(&op->nfs_resop4_u.opclose);               break;
    case rpcgen::OP_COMMIT:              nfs4_operation(&op->nfs_resop4_u.opcommit);              break;
    case rpcgen::OP_CREATE:              nfs4_operation(&op->nfs_resop4_u.opcreate);              break;
    case rpcgen::OP_DELEGPURGE:          nfs4_operation(&op->nfs_resop4_u.opdelegpurge);          break;
    case rpcgen::OP_DELEGRETURN:         nfs4_operation(&op->nfs_resop4_u.opdelegreturn);         break;
    case rpcgen::OP_GETATTR:             nfs4_operation(&op->nfs_resop4_u.opgetattr);             break;
    case rpcgen::OP_GETFH:               nfs4_operation(&op->nfs_resop4_u.opgetfh);               break;
    case rpcgen::OP_LINK:                nfs4_operation(&op->nfs_resop4_u.oplink);                break;
    case rpcgen::OP_LOCK:                nfs4_operation(&op->nfs_resop4_u.oplock);                break;
    case rpcgen::OP_LOCKT:               nfs4_operation(&op->nfs_resop4_u.oplockt);               break;
    case rpcgen::OP_LOCKU:               nfs4_operation(&op->nfs_resop4_u.oplocku);               break;
    case rpcgen::OP_LOOKUP:              nfs4_operation(&op->nfs_resop4_u.oplookup);              break;
    case rpcgen::OP_LOOKUPP:             nfs4_operation(&op->nfs_resop4_u.oplookupp);             break;
    case rpcgen::OP_NVERIFY:             nfs4_operation(&op->nfs_resop4_u.opnverify);             break;
    case rpcgen::OP_OPEN:                nfs4_operation(&op->nfs_resop4_u.opopen);                break;
    case rpcgen::OP_OPENATTR:            nfs4_operation(&op->nfs_resop4_u.opopenattr);            break;
    case rpcgen::OP_OPEN_CONFIRM:        nfs4_operation(&op->nfs_resop4_u.opopen_confirm);        break;
    case rpcgen::OP_OPEN_DOWNGRADE:      nfs4_operation(&op->nfs_resop4_u.opopen_downgrade);      break;
    case rpcgen::OP_PUTFH:               nfs4_operation(&op->nfs_resop4_u.opputfh);               break;
    case rpcgen::OP_PUTPUBFH:            nfs4_operation(&op->nfs_resop4_u.opputpubfh);            break;
    case rpcgen::OP_PUTROOTFH:           nfs4_operation(&op->nfs_resop4_u.opputrootfh);           break;
    case rpcgen::OP_READ:                nfs4_operation(&op->nfs_resop4_u.opread);                break;
    case rpcgen::OP_READDIR:             nfs4_operation(&op->nfs_resop4_u.opreaddir);             break;
    case rpcgen::OP_READLINK:            nfs4_operation(&op->nfs_resop4_u.opreadlink);            break;
    case rpcgen::OP_REMOVE:              nfs4_operation(&op->nfs_resop4_u.opremove);              break;
    case rpcgen::OP_RENAME:              nfs4_operation(&op->nfs_resop4_u.oprename);              break;
    case rpcgen::OP_RENEW:               nfs4_operation(&op->nfs_resop4_u.oprenew);               break;
    case rpcgen::OP_RESTOREFH:           nfs4_operation(&op->nfs_resop4_u.oprestorefh);           break;
    case rpcgen::OP_SAVEFH:              nfs4_operation(&op->nfs_resop4_u.opsavefh);              break;
    case rpcgen::OP_SECINFO:             nfs4_operation(&op->nfs_resop4_u.opsecinfo);             break;
    case rpcgen::OP_SETATTR:             nfs4_operation(&op->nfs_resop4_u.opsetattr);             break;
    case rpcgen::OP_SETCLIENTID:         nfs4_operation(&op->nfs_resop4_u.opsetclientid);         break;
    case rpcgen::OP_SETCLIENTID_CONFIRM: nfs4_operation(&op->nfs_resop4_u.opsetclientid_confirm); break;
    case rpcgen::OP_VERIFY:              nfs4_operation(&op->nfs_resop4_u.opverify);              break;
    case rpcgen::OP_WRITE:               nfs4_operation(&op->nfs_resop4_u.opwrite);               break;
    case rpcgen::OP_RELEASE_LOCKOWNER:   nfs4_operation(&op->nfs_resop4_u.oprelease_lockowner);   break;
    case rpcgen::OP_GET_DIR_DELEGATION:  nfs4_operation(&op->nfs_resop4_u.opget_dir_delegation);  break;
    case rpcgen::OP_ILLEGAL:             nfs4_operation(&op->nfs_resop4_u.opillegal);             break;
    }//switch
    }//if
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::ACCESS4args* args)
{
    if(args)
    {
        out << "access: " << args->access;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::ACCESS4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " supported: " << res->ACCESS4res_u.resok4.supported
                << " access: "    << res->ACCESS4res_u.resok4.access;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::CLOSE4args* args)
{
    if(args)
    {
        out <<  "seqid: "         << args->seqid
            << " open state id: " << args->open_stateid;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::CLOSE4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " open state id: " << res->CLOSE4res_u.open_stateid;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::COMMIT4args* args)
{
    if(args)
    {
        out <<  "offset: " << args->offset
            << " count: "  << args->count;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::COMMIT4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " write verifier: " << res->COMMIT4res_u.resok4.writeverf;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::CREATE4args* args)
{
    if(args)
    {
        out <<  "object type: "       << args->objtype
            << " object name: "       << args->objname
            << " create attributes: " << args->createattrs;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::CREATE4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " change info: " << res->CREATE4res_u.resok4.cinfo
                << " attributes set: " << res->CREATE4res_u.resok4.attrset;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::DELEGPURGE4args* args)
{
    if(args)
    {
        out << "client id: " << args->clientid;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::DELEGPURGE4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::DELEGRETURN4args* args)
{
    if(args)
    {
        out << "deleg state id: " << args->deleg_stateid;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::DELEGRETURN4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::GETATTR4args* args)
{
    if(args)
    {
        out << "attributes request: " << args->attr_request;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::GETATTR4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " object attributes: " << res->GETATTR4res_u.resok4.obj_attributes;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LINK4args* args)
{
    if(args)
    {
        out << "new name: " << args->newname;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LINK4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " change info: " << res->LINK4res_u.resok4.cinfo;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOCK4args* args)
{
    if(args)
    {
        out <<  "lock type: " << args->locktype
            << " reclaim: "   << args->reclaim
            << " offset: "    << args->offset
            << " length: "    << args->length
            << " locker: "    << args->locker;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOCK4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        switch(res->status)
        {
        case rpcgen::nfsstat4::NFS4_OK:
            out << " lock stat id: " << res->LOCK4res_u.resok4.lock_stateid; break;
        case rpcgen::nfsstat4::NFS4ERR_DENIED:
            out << " offset: "    << res->LOCK4res_u.denied.offset
                << " length: "    << res->LOCK4res_u.denied.length
                << " lock type: " << res->LOCK4res_u.denied.locktype
                << " owner: "     << res->LOCK4res_u.denied.owner;           break;
        default: break;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOCKT4args* args)
{
    if(args)
    {
        out <<  "lock type: " << args->locktype
            << " offset: "    << args->offset
            << " length: "    << args->length
            << " owner: "     << args->owner;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOCKT4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4ERR_DENIED)
        {
            out << " offset: "    << res->LOCKT4res_u.denied.offset
                << " length: "    << res->LOCKT4res_u.denied.length
                << " lock type: " << res->LOCKT4res_u.denied.locktype
                << " owner: "     << res->LOCKT4res_u.denied.owner;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOCKU4args* args)
{
    if(args)
    {
        out <<  "lock type: "     << args->locktype
            << " seqid: "         << args->seqid
            << " lock state id: " << args->lock_stateid
            << " offset: "        << args->offset
            << " length: "        << args->length;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOCKU4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " lock state id: " << res->LOCKU4res_u.lock_stateid;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOOKUP4args* args)
{
    if(args)
    {
        out << "object name: " << args->objname;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOOKUP4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::NVERIFY4args* args)
{
    if(args)
    {
        out << "object attributes: " << args->obj_attributes;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::NVERIFY4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::OPEN4args* args)
{
    if(args)
    {
        out <<  "seqid: "        << args->seqid
            << " share access: " << args->share_access
            << " share deny: "   << args->share_deny
            << " owner: "        << args->owner
            << " openhow: "      << args->openhow
            << " claim: "        << args->claim;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::OPEN4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " state id: "       << res->OPEN4res_u.resok4.stateid
                << " change info: "    << res->OPEN4res_u.resok4.cinfo
                << " rflags: "         << res->OPEN4res_u.resok4.rflags
                << " attributes set: " << res->OPEN4res_u.resok4.attrset
                << " delegation: "     << res->OPEN4res_u.resok4.delegation;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::OPENATTR4args* args)
{
    if(args)
    {
        out << "create directory: " << args->createdir;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::OPENATTR4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::OPEN_CONFIRM4args* args)
{
    if(args)
    {
        out << " open state id: " << args->open_stateid
            << " seqid: "         << args->seqid;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::OPEN_CONFIRM4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " open state id: " << res->OPEN_CONFIRM4res_u.resok4.open_stateid;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::OPEN_DOWNGRADE4args* args)
{
    if(args)
    {
            out << " open state id: " << args->open_stateid
                << " seqid: "         << args->seqid
                << " share access: "  << args->share_access
                << " share deny: "    << args->share_deny;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::OPEN_DOWNGRADE4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " open_stateid: " << res->OPEN_DOWNGRADE4res_u.resok4.open_stateid;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::PUTFH4args* args)
{
    if(args)
    {
        out << "object: " << args->object;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::PUTFH4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::READ4args* args)
{
    if(args)
    {
        out <<  "state id: " << args->stateid
            << " offset: "   << args->offset
            << " count: "    << args->count;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::READ4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " eof: " << res->READ4res_u.resok4.eof;
            if(res->READ4res_u.resok4.data.data_len)
                out << " data : " << *res->READ4res_u.resok4.data.data_val;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::READDIR4args* args)
{
    if(args)
    {
        out <<  "cookie: "             << args->cookie
            << " cookieverf: "         << args->cookieverf
            << " dir count: "          << args->dircount
            << " max count: "          << args->maxcount
            << " attributes request: " << args->attr_request;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::READDIR4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " cookie verifier: " << res->READDIR4res_u.resok4.cookieverf;
            out << " reply: "           << res->READDIR4res_u.resok4.reply;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::REMOVE4args* args)
{
    if(args)
    {
        out << "target: " << args->target;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::REMOVE4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " change info: " << res->REMOVE4res_u.resok4.cinfo;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::RENAME4args* args)
{
    if(args)
    {
        out <<  "old name: " << args->oldname
            << " new name: " << args->newname;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::RENAME4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " source change info: " << res->RENAME4res_u.resok4.source_cinfo
                << " target change info: " << res->RENAME4res_u.resok4.target_cinfo;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::RENEW4args* args)
{
    if(args)
    {
        out << "client id: " << args->clientid;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::RENEW4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SECINFO4args* args)
{
    if(args)
    {
        out << "name: " << args->name;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SECINFO4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            if(res->SECINFO4res_u.resok4.SECINFO4resok_len)
                out << " data : " << *res->SECINFO4res_u.resok4.SECINFO4resok_val;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SETATTR4args* args)
{
    if(args)
    {
        out <<  "state id: "          << args->stateid
            << " object attributes: " << args->obj_attributes;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SETATTR4res*  res)
{
    if(res)
    {
        out <<  "status: "         << res->status
            << " attributes set: " << res->attrsset;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SETCLIENTID4args* args)
{
    if(args)
    {
        out <<  "client: "         << args->client
            << " callback: "       << args->callback
            << " callback ident: " << args->callback_ident;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SETCLIENTID4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        switch(res->status)
        {
        case rpcgen::nfsstat4::NFS4_OK:
            out << " client id: " << res->SETCLIENTID4res_u.resok4.clientid
                << " set client if confirm: "
                << res->SETCLIENTID4res_u.resok4.setclientid_confirm;        break;
        case rpcgen::nfsstat4::NFS4ERR_CLID_INUSE:
            out << " client using: " << res->SETCLIENTID4res_u.client_using; break;
        default: break;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SETCLIENTID_CONFIRM4args* args)
{
    if(args)
    {
        out << " client id: "             << args->clientid
            << " set client if confirm: " << args->setclientid_confirm;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SETCLIENTID_CONFIRM4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::VERIFY4args* args)
{
    if(args)
    {
        out << "object attributes: " << args->obj_attributes;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::VERIFY4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::WRITE4args* args)
{
    if(args)
    {
        out <<  "state id: " << args->stateid;
        out << " offset: "   << args->offset;
        out << " stable: "   << args->stable;
        out << " data: ";
        if(args->data.data_len) out << *args->data.data_val;
        else out << "(empty)";
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::WRITE4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " count: "          << res->WRITE4res_u.resok4.count
                << " commited: "       << res->WRITE4res_u.resok4.committed
                << " write verifier: " << res->WRITE4res_u.resok4.writeverf;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::RELEASE_LOCKOWNER4args* args)
{
    if(args)
    {
        out << "lock owner: " << args->lock_owner;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::RELEASE_LOCKOWNER4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::GET_DIR_DELEGATION4args* args)
{
    if(args)
    {
        out <<  "client id: "                    << args->clientid
            << " notification types: "           << args->notif_types
            << " dir notification delay: "       << args->dir_notif_delay
            << " dir entry notification delay: " << args->dir_entry_notif_delay;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::GET_DIR_DELEGATION4res*  res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " state id: " << res->GET_DIR_DELEGATION4res_u.resok4.stateid
                << " status: " << res->GET_DIR_DELEGATION4res_u.resok4.status
                << " notification types: " << res->GET_DIR_DELEGATION4res_u.resok4.notif_types
                << " dir notification attributes: "
                << res->GET_DIR_DELEGATION4res_u.resok4.dir_notif_attrs
                << " dir entry notification attributes: "
                << res->GET_DIR_DELEGATION4res_u.resok4.dir_entry_notif_attrs;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::GETFH4res* res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " object: " << res->GETFH4res_u.resok4.object;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::LOOKUPP4res* res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::PUTPUBFH4res* res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::PUTROOTFH4res* res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::READLINK4res* res)
{
    if(res)
    {
        out << "status: " << res->status;
        if(res->status == rpcgen::nfsstat4::NFS4_OK)
        {
            out << " link: " << res->READLINK4res_u.resok4.link;
        }
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::RESTOREFH4res* res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::SAVEFH4res* res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::nfs4_operation(const struct rpcgen::ILLEGAL4res* res)
{
    if(res)
    {
        out << "status: " << res->status;
    }
}

void PrintAnalyzer::flush_statistics()
{
    // flush is in each handler
}

} // namespace analysis
} // namespace NST
//------------------------------------------------------------------------------
