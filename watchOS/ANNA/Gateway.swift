import CryptoKit
import Foundation
import Security

struct Pairing: Decodable {
    let url: URL
    let token: String
    let certificateSHA256: String

    static func load() -> Pairing? {
        guard let url = Bundle.main.url(forResource: "Pairing", withExtension: "json"),
              let data = try? Data(contentsOf: url) else { return nil }
        return try? JSONDecoder().decode(Pairing.self, from: data)
    }
}

private final class CertificatePin: NSObject, URLSessionDelegate {
    let fingerprint: String

    init(_ fingerprint: String) {
        self.fingerprint = fingerprint
    }

    func urlSession(_ session: URLSession, didReceive challenge: URLAuthenticationChallenge,
                    completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
        guard challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodServerTrust,
              let trust = challenge.protectionSpace.serverTrust,
              let chain = SecTrustCopyCertificateChain(trust) as? [SecCertificate],
              let leaf = chain.first else {
            completionHandler(.cancelAuthenticationChallenge, nil)
            return
        }
        let hash = SHA256.hash(data: SecCertificateCopyData(leaf) as Data)
        let actual = hash.map { String(format: "%02x", $0) }.joined()
        if actual == fingerprint {
            completionHandler(.useCredential, URLCredential(trust: trust))
        } else {
            completionHandler(.cancelAuthenticationChallenge, nil)
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil)
    }
}

enum AnnaError: LocalizedError {
    case message(String)

    var errorDescription: String? {
        switch self { case let .message(text): return text }
    }
}

final class Gateway {
    struct Batch: Decodable {
        let events: [String]
        let audio: [Data]
    }

    struct Notice: Decodable, Identifiable {
        let id: Int64
        let text: String
    }

    private struct SessionID: Decodable { let id: String }

    private let pairing: Pairing
    private let session: URLSession

    init(pairing: Pairing) {
        self.pairing = pairing
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 5
        config.timeoutIntervalForResource = 7
        session = URLSession(configuration: config, delegate: CertificatePin(pairing.certificateSHA256),
                             delegateQueue: nil)
    }

    private func request(_ path: String, method: String = "GET", body: Data? = nil,
                         json: Bool = false, release: Bool = false, since: Date? = nil) async throws -> Data {
        guard pairing.url.scheme == "https",
              var url = URLComponents(url: pairing.url.appending(path: path), resolvingAgainstBaseURL: false)
        else { throw AnnaError.message(String(localized: "Invalid Mac pairing address")) }
        if release { url.queryItems = [URLQueryItem(name: "release", value: "1")] }
        if let since {
            url.queryItems = [URLQueryItem(name: "since", value: String(since.timeIntervalSince1970))]
        }
        guard let endpoint = url.url else { throw AnnaError.message(String(localized: "Invalid Mac pairing address")) }
        var request = URLRequest(url: endpoint)
        request.httpMethod = method
        request.setValue("Bearer " + pairing.token, forHTTPHeaderField: "Authorization")
        if body != nil {
            request.setValue(json ? "application/json" : "application/octet-stream",
                             forHTTPHeaderField: "Content-Type")
        }
        request.httpBody = body
        let (data, response) = try await session.data(for: request)
        switch (response as? HTTPURLResponse)?.statusCode {
        case 200, 201, 204: return data
        case 401: throw AnnaError.message(String(localized: "Mac pairing expired"))
        case 409: throw AnnaError.message(String(localized: "Dot is in use on another device"))
        case 404: throw AnnaError.message(String(localized: "Conversation disconnected. Try again."))
        default: throw AnnaError.message(String(localized: "Mac gateway unavailable"))
        }
    }

    func health() async -> Bool {
        (try? await request("watch/health")) != nil
    }

    func notice(since: Date) async throws -> Notice? {
        let data = try await request("watch/notices", since: since)
        return data.isEmpty ? nil : try JSONDecoder().decode(Notice.self, from: data)
    }

    func acknowledge(_ id: Int64) async throws {
        _ = try await request("watch/notices/" + String(id), method: "DELETE")
    }

    func open(mode: String) async throws -> String {
        let body = try JSONEncoder().encode(["mode": mode])
        let data = try await request("watch/sessions", method: "POST", body: body, json: true)
        return try JSONDecoder().decode(SessionID.self, from: data).id
    }

    func exchange(_ id: String, pcm: Data = Data(), release: Bool = false) async throws -> Batch {
        let data = try await request("watch/sessions/" + id, method: "POST", body: pcm, release: release)
        return try JSONDecoder().decode(Batch.self, from: data)
    }

    func close(_ id: String) async {
        _ = try? await request("watch/sessions/" + id, method: "DELETE")
    }
}
